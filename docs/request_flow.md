# 从请求到响应：代码阅读导航

本页只解释现有实现，不增加框架或新功能。按调用流程阅读，不必按 server.cpp 中函数的物理排列逐行读。

## 1. 启动阶段

```text
main（事件线程）
  ├─ MYWEB_AUTH=1 时创建 AuthService
  │    ├─ Library 初始化 MySQL 客户端库
  │    ├─ SqlConnPool 建立 4 条连接
  │    └─ prepare 查询，检查 users 表/列
  └─ Server::Init
       ├─ socket → bind → listen → 非阻塞
       ├─ Epoller::init / ADD(listenfd)
       └─ InitWorkers
            ├─ eventfd → ADD(completedFd)
            └─ ThreadPool(4, 64) → 4 个 WorkerLoop
```

启动时建连和表检查在主线程；请求期间的数据库操作在工作线程。Epoller、Connection、AuthService 不是额外线程。

## 2. 一条注册请求的完整路径

| 顺序 | 线程 | 入口与动作 | 数据/阶段 |
| --- | --- | --- | --- |
| 1 | 事件线程 | run → AcceptClients | accept，新建 fd/id 对应的 Connection，READING |
| 2 | 事件线程 | run → HandlerClientEvent | recv 的字节追加到 conn.input；成功收发才刷新 lastActive |
| 3 | 事件线程 | ParseAndSubmit → HttpParser::Parse | 请求行/头/正文检查；NeedMore 则继续 READING |
| 4 | 事件线程 | ParseAndSubmit → ThreadPool::Submit | 进入 PROCESSING，暂停普通读写关注，复制请求并入队 |
| 5 | 工作线程 | WorkerLoop → task() → BuildResponse | 识别 /register，解析 URL-encoded 表单 |
| 6 | 同一个工作线程 | AuthService::Register | HashPwd → ThreadScope → SqlConnLease → 预处理 INSERT |
| 7 | 同一个工作线程 | BuildResponse 返回 | Registered/Conflict/Unavailable 转为 HTTP 201/409/503 字节 |
| 8 | 同一个工作线程 | PublishCompletion | fd/id/output/ok 入完成队列，写 eventfd |
| 9 | 事件线程 | run → HandleCompletions | 清通知、取结果，检查连接存在/id/PROCESSING |
| 10 | 事件线程 | HandleCompletions → epoller.MOD | output 接管响应，sent=0，WRITING，关注 EPOLLOUT |
| 11 | 事件线程 | HandlerClientEvent 发送分支 | send 累计 sent；EAGAIN 保留进度，EINTR 重试 |
| 12 | 事件线程 | 发送结束 | 关闭，或重置为 READING 并立即解析已缓存下一请求 |

一个函数调用不意味着新线程。Register、HashPwd、MySQL C API 都是执行该任务的同一个 worker 的普通调用；真正的异步交接点是 Submit 和完成队列/eventfd。

## 3. 三个阶段与三个关键迁移点

```text
新连接 → READING
           ├─ NeedMore / recv EAGAIN → 仍 READING
           └─ 解析结束 → ParseAndSubmit → PROCESSING
                                           │
                                 成功完成结果被事件线程应用
                                           ↓
                                        WRITING
                                           ├─ send EAGAIN → 仍 WRITING
                                           ├─ 可复用 → READING
                                           └─ 不复用/已半关闭 → CloseClient
```

- PROCESSING 包括任务排队与执行，不表示 CPU 一定正在运行这项任务。
- PROCESSING 时 HandlerClientEvent 返回，但 run 继续处理其他事件；完成通知直接分发到 HandleCompletions，不经过客户端的阶段判断。
- Keep-Alive、peerWriteClosed 是独立属性，不是互斥阶段。后续请求已在 input 中时，可立即从 READING 再转 PROCESSING，无需新 EPOLLIN。
- 关闭通过注销、close、从 connections_ 删除表达，没有 CLOSED 枚举。

## 4. 入口职责与返回值

| 入口 | 职责/返回含义 |
| --- | --- |
| run | Wait(1000)；区分监听、完成通知、客户端；批次之后检查超时 |
| HandlerClientEvent | 按阶段收发，处理错误/半关闭；不执行 Auth 或文件业务 |
| ParseAndSubmit | true：仍可继续，可能 NeedMore 或已提交；false：连接已关闭，调用者立即返回 |
| ThreadPool::Submit | 是否接受任务；不等待队列空位或任务结果，但入队仍使用短时 mutex |
| WorkerLoop | 条件变量等任务，加锁取出后解锁，才执行 task() |
| BuildResponse | 解析结果/路由 → 完整 HTTP 响应字符串；401/409 也是正常构造结果 |
| PublishCompletion | completed_ 保存结果，eventfd 只通知；二者不要求逐项计数对应 |
| HandleCompletions | 成功应用结果时进入 WRITING；过期身份丢弃；构造失败关闭 |
| CloseClient | DEL → close → erase，且只由事件线程调用 |
| ExpireIdleClients | 单调时钟、每秒扫描、30 秒网络无进展关闭；不会取消 worker |

## 5. 所有权与线程边界

```text
main 的 unique_ptr<AuthService> 拥有业务服务
  └─ AuthService：Library、SqlConnPool、dummyHash_

Server 借用 AuthService*（不拥有）
  ├─ 拥有客户端连接表与客户端 fd
  ├─ 拥有监听 fd、完成通知 fd、Epoller
  └─ 拥有 ThreadPool

工作任务持有请求副本、fd/id 值
  └─ 不持有 Connection&，不操作客户端 socket/epoll/连接阶段
```

- 任务队列锁只保护入队/取任务，不包住业务执行。
- 完成队列锁保护结果交接，不允许 worker 直接修改连接。
- 数据库池锁只保护借还；每条借出的 MYSQL* 独占执行 SQL。
- 请求任务中的语句先关闭，再归还 lease，最后清理 ThreadScope。
- 普通析构路径：Server 先排空/join worker，再释放通知资源；main 后释放 AuthService，关闭数据库连接，最后结束 MySQL 库。默认 SIGTERM/Ctrl+C 不是这条析构路径。

## 6. 其他请求只在业务分支不同

- `/login`：SELECT 哈希记录，归还连接后 VerifyPwd；不存在用户也做 dummy 校验，但永远不据此通过。
- `/static/...`：LoadStaticFile 检查路径/MIME，逐级 openat、不跟随链接、fstat、读取普通文件；返回前关闭文件 fd，内容已在字符串中。
- `/`：读取固定 resources/index.html，浏览器再单独请求 CSS/JS/PNG。
- `/echo`、`/form`：正文回传或普通表单处理。

这些分支的前后网络路径一致。CMake 只决定源文件怎样编译链接，不参与运行中的一次请求。

## 7. 三类结果不要混淆

| 名称 | 回答什么 |
| --- | --- |
| HttpParseResult | 请求字节是否完整/合法 |
| AuthResult | 账号密码业务结果 |
| ConnectionState | 连接当前应读、等任务还是写 |
| CompletedResponse::ok | 响应构造是否成功，不是 HTTP 是否为 200 |
| epoll ready 标志 | 内核现在报告的 I/O 条件，不是业务阶段 |

异常、超时或对端离开可发生在任一阶段。fd 可能复用，所以完成结果必须同时检查 id。超时关闭不取消旧任务；任务即使随后完成，也不能将结果交给新连接。

## 8. 建议的复习方式

先画出 `recv → ParseAndSubmit → task → BuildResponse → PublishCompletion → HandleCompletions → send`，给每一段标线程，再分别代入一个完整请求、一个半请求、一个慢响应和一个提前断开的客户端。能够解释数据存在哪里、由谁改变、何时释放，比背所有系统调用参数更重要。
