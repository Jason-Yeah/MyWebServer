# 阶段总结：从单个 main.cpp 到单线程 LT epoll 服务器

> 本文中的“stage_1”是第一段学习历程的总结，不等同于 `spec.md` 中仅包含最小 TCP 服务端的“阶段 1”。本文覆盖从 socket 入门到 LT epoll 及 Epoller / Server 封装的全过程。详细规划与验收记录见 [spec.md](spec.md)。

## 1. 我们最终在解决什么问题

最初的问题是：**怎样让程序接受一个 TCP 连接？**

逐步变成：

1. 怎样在 TCP 上传输浏览器能理解的 HTTP 响应？
2. 怎样正确处理请求被拆成多次到达的情况？
3. 怎样让一个没有发完请求的客户端不阻塞其他客户端？
4. 怎样在不能一次发完响应时保留发送进度？
5. 怎样明确每个 fd 和每份连接状态由谁管理、何时释放？

我们没有一开始就设计完整架构，而是在出现具体问题后引入对应机制：

```text
单个 main：socket → bind → listen → accept → close
    ↓ 需要让浏览器理解返回内容
阻塞 HTTP：recv → 解析最小请求 → 构造响应 → send
    ↓ 请求可能分多次到达
Buffer + HttpRequest + HttpParser
    ↓ 一个慢客户端会阻塞整个串行服务器
非阻塞 socket + LT epoll
    ↓ 多个连接需要分别保存读写进度
Connection + BuildResponse
    ↓ main 同时承担过多职责，资源清理容易遗漏
Epoller + Server，main 退回启动入口
```

**封装解决职责和生命周期问题；非阻塞与 epoll 解决网络等待和事件调度问题。它们不是同一件事。**

## 2. 起点：先在一个 main 中理解 TCP 生命周期

### 2.1 最小服务端做了什么

最初把流程直接写在 `main.cpp` 中：

```text
socket → bind → listen → accept → 处理连接 → close
```

| 调用 | 作用 | 返回值与需要注意的资源 |
| --- | --- | --- |
| `socket` | 创建通信端点 | 成功返回 fd，失败返回 -1；成功后需要关闭 |
| `bind` | 将 socket 绑定到本地地址和端口 | 成功返回 0，失败返回 -1；不会替我们关闭 socket |
| `listen` | 将 socket 变成监听 socket | 成功后可接受连接；它本身不处理 HTTP |
| `accept` | 从等待队列中取出一个连接 | 返回新的客户端 fd，监听 fd 继续保留 |
| `close` | 释放当前进程持有的 fd | 监听 fd 与各客户端 fd 都要分别回收 |

监听地址限定为 `127.0.0.1:8080`，不主动对外开放。`htons` / `htonl` 用于把端口和地址转换成网络字节序。

这里最重要的区分是：

```text
listenfd：负责接受新连接
clientfd：负责与某一个客户端收发数据
```

### 2.2 为什么没有一开始就封装 Server

因为这时最需要看清系统调用之间的先后关系：失败发生在哪一步，当前已经取得哪些资源，退出前应该关闭哪些 fd。

如果过早引入多个类，容易记住 `server.Start()`，却不理解里面的 socket 是何时创建的。

最初 `accept` 到连接后直接关闭，只能证明 TCP 服务端能接受连接。此时 curl 提示空响应，并不代表 TCP 实验失败，因为还没有实现 HTTP。

## 3. 从 TCP 到阻塞 HTTP：连接成功不等于页面可访问

### 3.1 为什么要构造 HTTP 响应

TCP 提供的是可靠、有序的字节流，不知道什么是网页、状态码或表单。

浏览器需要按 HTTP 格式解释字节，例如：

```text
HTTP/1.1 200 OK\r\n
Content-Type: text/plain; charset=utf-8\r\n
Content-Length: 5\r\n
Connection: close\r\n
\r\n
hello
```

响应必须明确：状态、正文类型、正文长度，以及头部在哪里结束。`Content-Length` 按字节计算，不是按中文字符个数计算。

随后支持了首页、404、文件读取失败时的 500，以及空文件响应。

### 3.2 为什么 send 不能只调用一次就认为成功

`send(fd, data, length, flags)` 的正返回值是“这次交给内核多少字节”，可能小于 `length`，不保证对端应用已经读取。

因此需要维护剩余数据：

```text
发送起点 = 数据首地址 + 已发送字节数
本次长度 = 数据总长度 - 已发送字节数
```

阻塞版可以在当前连接内循环发送；后来的非阻塞版则必须允许暂停并等待下一次可写事件。

`MSG_NOSIGNAL` 用于避免给已关闭的连接发送数据时，SIGPIPE 默认终止整个服务器。它不意味着发送一定成功，仍要检查返回值和 `errno`。

### 3.3 阻塞串行版的局限

```text
accept A → recv A → 处理 A → 发送 A → 关闭 A → accept B
```

这条路径容易理解，但如果 A 一直不把请求发完，程序就可能停在 `recv A`，B 无法及时得到处理。

这时还不急着引入 epoll：先把 HTTP 的正确性做清楚，否则更换并发方式只会把原来的解析问题带到多连接环境中。

## 4. 引入 Buffer：一次 recv 不等于一个请求

TCP 没有“请求包”的边界。客户端发送一个完整请求，服务端可能分多次收到；一次接收也可能包含请求头和部分或全部正文。

例如：

```text
第一次收到：GET /ind
第二次收到：ex.html HTTP/1.1\r\nHost: localhost\r\n\r
第三次收到：\n
```

若每次只解析当前 `recv` 的临时数组，就无法保留前两次的数据。

### 4.1 Buffer 的职责

`Buffer` 管理收到但尚未消费的字节：

- `Append`：追加新数据。
- `Peek`：查看未消费区域的起点。
- `ReadableBytes`：查询未消费字节数。
- `Retrieve`：标记已消费的字节。

它不理解 HTTP，只负责字节的保存和消费位置。

```text
socket → recv 临时数组 → Buffer → 协议解析器
```

这将“怎样保存数据”和“怎样解释协议”分开。解析器消费完整头部后，Buffer 中剩下的字节还可以继续作为正文使用。

## 5. 引入 HttpRequest / HttpParser：保存结果与保存进度是两件事

### 5.1 HttpRequest 保存请求内容

`HttpRequest` 保存方法、路径、版本、头字段、正文，并提供请求行、头字段及正文长度规则等相关处理。

它回答的是：**请求里有什么？**

### 5.2 HttpParser 保存解析进度

解析器不能把“数据还没到齐”当成“请求错误”。因此解析结果不再只是 true / false，而包含：

```text
NeedMore：继续等待后续数据
Complete：本次请求已经完整
各种错误：拒绝请求，构造对应响应
```

内部状态逐步推进：

```text
Headers → Body → Complete
    └──── 遇到错误 → Failed
```

它回答的是：**现在解析到哪里，下次应从哪里继续？**

例如正文声明长度为 5，当前只有 `he`，状态应保留在正文阶段；收到 `llo` 后才完成，不应提前回显或重新解析请求头。

### 5.3 为什么要先明确请求边界和错误规则

如果长度规则含糊，服务器就不知道应该等多少正文，也可能把多余字节误认为其他内容。

当前实现加入了：

- 请求行和头字段格式检查。
- Host 的存在、非空和重复检查。
- 头部大小上限、64 KiB 正文上限。
- 非法 Content-Length、冲突的长度机制和不支持的 Transfer-Encoding 检查。
- `/echo` 文本回显、`/form` 表单解码及路由方法/类型检查。

这仍是受限 HTTP 实现，不是完整协议实现：不支持 chunked、连接复用和流水线；Host 也没有实现完整语法校验。

## 6. 为什么引入非阻塞和 epoll

协议正确后，串行服务器仍有同一个问题：**网络等待会占住唯一的执行线程。**

### 6.1 仅改成非阻塞还不够

设置 `O_NONBLOCK` 后，socket 没有数据可读时，`recv` 不再一直等待，而是返回 -1，并用 `errno` 报告 EAGAIN / EWOULDBLOCK。

设置时使用：

```cpp
int flags = fcntl(fd, F_GETFL, 0);
fcntl(fd, F_SETFL, flags | O_NONBLOCK);
```

先获取原状态，再按位或增加非阻塞标志，避免直接覆盖原有状态。监听 socket 和接受到的客户端 socket 都要设置；不能假定 accept 得到的 socket 自动继承非阻塞状态。

但如果程序不断轮流尝试所有连接：

```text
试读 A → EAGAIN → 试读 B → EAGAIN → 再试 A → …
```

就会忙等，浪费 CPU。

### 6.2 epoll 负责通知“现在值得尝试哪些操作”

我们选择 Linux 的 epoll，把关注的 fd 和事件交给内核：

```text
添加/修改关注列表 → epoll_wait 等待 → 获取就绪事件 → 尝试相应 I/O
```

`epoll_wait` 本身可以阻塞，因为它等待的是**所有已关注连接中的任意就绪事件**，而不是卡在某个慢客户端身上。

| 接口 | 当前作用 |
| --- | --- |
| `epoll_create1` | 创建一个 epoll 实例，返回需要关闭的 fd |
| `epoll_ctl` + ADD | 添加关注的 socket |
| `epoll_ctl` + MOD | 修改这个 socket 关注的事件 |
| `epoll_ctl` + DEL | 取消关注，不等于关闭 socket |
| `epoll_wait` | 将本次就绪事件写入事件数组，返回有效数量；失败返回 -1 |

epoll 并没有替我们读取请求、解析 HTTP 或发送响应，也没有创建处理线程。

**当前仍只有一个执行线程，只是它不再被一个没有数据的 socket 长时间占住。**

### 6.3 为什么先学习 LT

LT 是水平触发，也是未指定 EPOLLET 时的默认模式。只要就绪条件持续成立，后续等待仍可返回这个 fd 的事件。

所以当前每次事件只调用一次 `recv` 或 `send` 是合法的：

```text
还有数据没读完 → 后续仍可得到可读通知
还有响应没发完且仍可写 → 后续仍可得到可写通知
```

这便于先验证多连接状态保存和资源回收，再学习 ET 的读写耗尽策略。不能把当前实现直接加上 EPOLLET 就认为迁移完成。

## 7. 引入 Connection：多个连接不能共享一份进度

串行处理一个连接时，局部变量可以一直留在当前处理过程里。

引入事件循环后，处理 A 一部分就可能去处理 B，随后才再次收到 A 的事件。若每次事件都重新创建解析器，A 的半个请求就丢失了。

因此引入连接表：

```cpp
std::unordered_map<int, Connection> connections_;
```

以客户端 fd 为键，每个连接分别保存：

| 成员 | 含义 | 为什么必须跨事件保留 |
| --- | --- | --- |
| `input` | 输入 Buffer | 请求可能分多次到达 |
| `parser` | 请求和解析状态 | 不能重复解析或丢失正文进度 |
| `output` | 完整响应字符串 | 响应可能一次发不完 |
| `sent` | 已发送字节数 | 下次必须从未发送的位置继续 |
| `responding` | 是否已进入发送阶段 | 区分当前应该接收请求还是发送响应 |

Connection 保存状态，不负责关闭 socket；当前客户端 fd 的所有者是 Server。

## 8. 提取 BuildResponse：业务响应不应该绑在事件循环里

原来的 main 里混有：解析结果判断、首页文件读取、echo/form 路由和 HTTP 响应头拼装。

改用 epoll 时，这些业务规则本身并没有改变，因此提取为：

```cpp
std::string BuildResponse(
    const HttpRequest& request,
    HttpParseResult parseResult
);
```

它接收请求及最终解析结果，返回待发送的完整响应，不管理 fd，不调用 epoll，不负责网络发送。

```text
接收和解析是否完成 → Server / HttpParser
返回什么内容       → BuildResponse
什么时候发送       → Server + epoll
发送到哪里、多少了 → fd + Connection::sent
```

只有解析结果不再是 NeedMore 时才调用它。错误结果也构造错误响应，不能落入首页成功分支。

注意：当前文件读取和响应构造仍同步执行。网络等待不会卡住其他客户端，不代表磁盘读取已经异步化。

## 9. 当前 LT 连接完整路径

```text
监听 socket 可读
  → accept，设置客户端非阻塞
  → 注册 EPOLLIN | EPOLLRDHUP
  → 创建 Connection

客户端可读
  → recv
  → Append 到该连接的 Buffer
  → parser.Parse
      ├─ NeedMore：保留状态，等待后续事件
      └─ Complete / 错误：BuildResponse
                           → sent = 0
                           → responding = true
                           → 改关注 EPOLLOUT

客户端可写
  → send(output.data() + sent, output.size() - sent, ...)
      ├─ 正数：sent 增加实际返回字节数
      ├─ EAGAIN：保留 output 和 sent，等待下次事件
      ├─ EINTR：重试当前系统调用
      └─ 其他失败：关闭连接
  → 全部发送完成：关闭连接
```

### 9.1 为什么不始终订阅 EPOLLOUT

大部分空闲 TCP socket 经常是可写的。若没有待发送数据也一直订阅，LT 模式可能反复通知可写，使事件循环无意义地忙碌。

所以只在已经构造响应后改为关注 EPOLLOUT；当前响应发送完成就关闭连接。

### 9.2 如何理解 EOF、半关闭和错误

| 情况 | 当前处理原则 |
| --- | --- |
| `recv > 0` | 有数据，追加并解析 |
| `recv == 0` | 对端发送方向已结束；当前请求若仍未完成则关闭，不返回成功响应 |
| `recv/send < 0` 且 EINTR | 被信号中断，重试 |
| `recv/send < 0` 且 EAGAIN / EWOULDBLOCK | 当前不能继续，不视为请求错误，也不清空进度 |
| EPOLLERR 或不可恢复 I/O 错误 | 关闭连接 |

EPOLLRDHUP 表示对端关闭了发送方向，但不代表不能接收响应。FIN 到达时，内核中还可能有请求数据未读，不能仅看这个标志就立刻丢弃连接。

当前实现先按事件尝试读取：完整请求解析成功后仍进入响应阶段。已通过“完整 GET / 64 KiB echo 请求后 SHUT_WR”的验证。

## 10. 为什么最后才封装 Epoller 和 Server

到这里 main 已同时承担初始化、事件注册、连接表、HTTP 处理和资源回收。继续增加功能时，很容易混淆：

- 哪个 fd 属于谁？
- 注册失败后谁关闭新连接？
- 初始化中途失败后哪些资源已经取得？
- 删除连接后能否继续访问原来的 Connection 引用？

这时系统流程已经跑通，适合按明确职责迁移，而不是为了“看起来面向对象”提前拆文件。

### 10.1 Epoller：封装事件机制

管理 epoll fd、就绪事件数组和本次有效数量，提供 init / ADD / MOD / DEL / Wait / Event。

- 析构只关闭 epoll fd。
- 不拥有注册进去的 socket。
- 系统调用失败通过返回值报告，不依赖 assert 处理资源不足。
- Event 的断言检查调用方下标是否在本次有效事件范围内，而不是只看数组容量。

### 10.2 Server：封装服务器生命周期和调度

| 成员函数 | 从旧 main 迁入的职责 |
| --- | --- |
| `Init` | socket、地址、bind/listen、非阻塞、初始化 epoll、注册监听事件 |
| `run` | 等待并分发事件 |
| `AcceptClients` | accept 循环、配置客户端、注册事件、创建连接状态 |
| `HandlerClientEvent` | 接收、解析、构造响应、发送、错误处理 |
| `CloseClient` | 取消关注、关闭 socket、删除连接状态 |
| 析构 | 关闭尚存的客户端和监听 socket；成员 Epoller 自动析构 |

main 最终只剩：

```cpp
int main()
{
    Server server;
    if (!server.Init())
        return 1;
    return server.run();
}
```

HTTP 行为和 LT 策略没有改变，只是代码职责和资源归属更清楚了。

### 10.3 迁移过程中最值得记住的几个错误

1. **创建 C++ 对象，不等于创建了内核 epoll 实例。** Epoller 使用默认构造函数，因此必须先调用 init，再 ADD。
2. **有效 fd 包含 0。** 判断应为 `fd >= 0`，不是 `fd > 0`。
3. **不能既提前关闭，又让成员保留旧值等待析构再关闭。** Init 失败时由 Server 析构统一回收已取得的资源；尚未交给连接表管理的 clientfd 则由当前失败分支立即关闭。
4. **从事件循环搬进函数后，跳过当前事件的 continue 要变成 return。** accept 循环自身的 continue 则保留。不能机械替换所有循环控制语句。
5. **关闭并删除连接后，原来的 Connection 引用失效。** 必须立即停止使用它。
6. **拥有 fd 的对象不能随意复制。** 复制 fd 数值不会复制内核资源，两个对象都关闭同一数值可能误关其他已复用的资源，因此禁止复制 Epoller 和 Server。
7. **新 .cpp 必须加入 CMake 目标。** 头文件中的声明不等于链接时已经有实现。

## 11. 当前实际验证到了哪里

封装后的版本已执行：

- CMake 构建和 5/5 CTest，包括 Buffer、请求解析、表单解析及资源生命周期测试。
- 六份原有 Python 网络脚本，覆盖拆分请求、头字段、错误请求、正文长度与表单等场景。
- 慢客户端 A 只发送部分请求头或正文时，B 仍可完成请求；A 补齐后也成功。
- 完整请求后半关闭、截断 EOF、异常 RST、发送中断连。
- 隔离目录中的 8 MiB 响应与预期正文逐字节比较，确认不丢失、不重复；不改动原首页。
- fd 数量恢复基线；Epoller 析构不关闭被观察的 socket；Server 初始化失败及正常离开作用域的回收。
- fd 为 0、重复初始化、真实 EMFILE，以及注入 epoll_wait 错误后 run 返回并析构存活连接。

最新发送探针记录 **181 次真实部分发送、2 次注入 EAGAIN、0 次自然内核发送 EAGAIN**。因此可以说发送偏移和注入后的恢复通过，不能说已经自然触发发送 EAGAIN。

测试中也修正过测试方法：不能只凭固定延时后的 fd 数量判断服务器仍在发送。启动探针现在完成一次 HTTP 请求后才算就绪，慢接收客户端在握手前限制接收窗口，避免测试前提受调度和内核缓冲自动调优影响。

### 11.1 验证命令

在 `MyWebServer/` 下，确认 8080 没有其他服务占用后执行：

```sh
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure
python3 tests/lt_network.py
cc -shared -fPIC -Wall -Wextra tests/send_probe.c -ldl -o build/send_probe.so
python3 tests/lt_network.py --send-probe build/send_probe.so
```

网络脚本自行启动并回收测试进程；生命周期 CTest 也需要空闲的 8080。不要为测试结束用户原有服务。完整命令与更细的覆盖边界见 spec.md。

### 11.2 不要混淆几种“回收成功”

- Ctrl+C / SIGTERM 默认终止进程时，内核会释放进程资源，但不会正常执行局部 C++ 对象析构。
- 本轮通过对象作用域结束及 run 错误返回，专门验证了析构路径；这不等于实现了优雅停机。
- fd 恢复基线说明本轮检查的描述符资源得到回收，不等于所有内存泄漏都已排除。尚未做 ASan / 内存泄漏专项。

## 12. 本阶段没有做什么，下一步为什么是 ET

当前仍然是：

- 单线程，非阻塞 socket，LT epoll。
- 每个连接只处理一个请求，响应声明 Connection: close。
- 文件读取和响应构造同步执行。
- 没有读取超时、连接数上限、线程池、数据库。
- 没有完整 HTTP 合规性、Keep-Alive、流水线或 chunked 支持。

下一步学习 ET，是为了对照理解事件通知语义：为什么 LT 中“一次没读完以后还有通知”的策略，不能原样搬到 ET；为什么需要配套设计读取和发送循环、EAGAIN 返回及状态保存。

**不是先假定 ET 更快，也不是只加一个 EPOLLET 宏。** 下一单元先保持 HTTP 协议与连接所有权不变，由用户实现事件策略变化，再复用当前 LT 验收基线进行比较。

---

## 回顾时应能回答的六个问题

1. socket 只提供 TCP 字节流，为什么还需要 HTTP 格式？
2. Buffer 保存字节，HttpParser 保存进度，为什么两者不能混为一谈？
3. 非阻塞解决“不等待”，epoll 解决“等谁就绪”，两者为什么需要配合？
4. 事件处理函数返回后，为什么半个请求和发送偏移仍不能丢？
5. 为什么 epoll 删除关注与 close socket 是两个操作？
6. 为什么 main 变短不是目的，明确职责和资源所有权才是封装的收益？
