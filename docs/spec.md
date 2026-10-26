# MyWebServer 学习规划

## 1. 目标与边界

以 `../../WebServer/` 为参考，从零实现一个小型 Linux C++ HTTP 服务器。重点不是复刻页面或追求 QPS，而是能够解释请求的完整路径、资源生命周期、并发方式和错误处理。

工作顺序：

1. 审查参考项目对应模块：设计是否合理，代码是否正确。
2. 理解并实现合理的功能，不继承已知缺陷。
3. 为功能和边界情况提供可重复验证。
4. 基础功能稳定后，只增加少量有学习价值的改进。

本项目不以达到生产级安全性、兼容所有 HTTP 客户端或实现完整网站业务为目标。

当前协作方式：用户基础较弱，需要先解释设计动机、数据流和系统接口，再给出明确的文件与修改位置；每轮按一个完整功能单元推进，避免拆成过多细碎步骤。常规单元测试、网络回归和必要的调试由 agent 执行，不反复要求用户粘贴长命令。未经明确授权，不替用户实现整批新业务代码。

## 2. 当前状态与环境

- [x] 参考项目已编译运行；用户已验证登录欢迎页、图片显示和视频资源访问。
- [x] WSL2 / Ubuntu 24.04 环境具备 G++ 13、CMake、Make、GDB、MySQL 8 及开发库。
- [x] 创建学习目录及规划文档。
- [x] 完成最小 TCP 服务端，并接入 CMake。
- [x] 阶段 2：已验证首页、404、500、空文件及异常断连。
- [x] 阶段 3 的当前学习范围：Buffer、增量状态机、受限 HTTP/1.1 GET/POST、正文长度规则、文本和表单接口；单元及网络回归通过。
- [x] 阶段 4 预备实验：`nonblocking_demo` 已观察到 EAGAIN、epoll 无事件和发送后的可读事件（用户运行确认）。
- [x] 阶段 4 初版代码：已提取 BuildResponse、增加 Connection，并将 main.cpp 改为非阻塞 LT epoll；agent 本次构建及 4 项 CTest 通过。
- [x] 阶段 4 LT 子单元验收：最新 LT 版本已通过 6 份网络回归、慢客户端隔离、半关闭、RST、8 MiB 响应一致性及 fd 回收检查；真实部分发送和注入 EAGAIN 已覆盖。自然发生的发送 EAGAIN 未观察到，详见末尾记录。整个阶段仍包含尚未完成的 ET / Keep-Alive。
- [x] 阶段 4 结构封装：用户已将事件循环迁移到 Server、封装 Epoller；agent 已完成 5/5 CTest、完整 LT 网络回归和资源生命周期验收，详见末尾记录。
- [x] 阶段 4 ET 子单元：用户已实现模式选择、三处事件掩码和读写循环；agent 修复错误分支后完成 6/6 CTest、LT/ET 对照网络验收，观察到真实接收和发送 EAGAIN，详见末尾记录及 [et_plan.md](et_plan.md)。
- [x] 阶段 4 收尾：最小显式选择 Keep-Alive 已实现并通过 LT/ET 验收；每连接最多 16 次请求，默认仍关闭，完整范围与限制见末尾记录。阶段 4 的当前教学范围完成。
- [x] 阶段 5 独立线程池：有界队列、互斥锁、条件变量、Close 排空与析构 join 已实现；7/7 CTest、专项重复 20 轮和独立 ASan/UBSan 通过。
- [x] 阶段 5 当前教学范围：响应构造线程池接入及定向生命周期验收通过；9/9 CTest、LT/ET 并发专项各重复 10 轮、集成专项 ASan/UBSan 和正式网络回归通过。测试替身、注入方式及未覆盖范围见末尾记录。
- [x] 阶段 6 超时基本功能：steady_clock + 周期扫描、真实 30 秒网络超时及续期、后台超时结果丢弃验证通过；首次真实 ET 测试的最终 fd 断言曾失败，后续未复现，保留待观察项，不宣称已定位原因。
- [x] 阶段 6 基础同步日志：完成 errno 保留、整行同步、64 位 id 和关键分支接入修正；10/10 CTest、日志专项重复 10 次、LT/ET 日志集成及网络/Keep-Alive/真实超时回归通过。
- [x] 阶段 7 单连接查询基本闭环：db_smoke 已构建，agent 验证缺少凭据时明确退出；用户在本机提供 PASS database SELECT 1 输出，确认其学习账号可进行 TCP 认证及固定查询。没有建表或数据库写入；未定向测试查询中断等故障。
- [x] 阶段 7 连接池机制专项：用户实现 SqlConnPool/SqlConnLease，测试替身验证独占、耗尽、异常归还、初始化回滚及生命周期误用；11/11 CTest、专项重复 20 次和专项 ASan/UBSan 通过。这不是实际数据库并发查询验收。
- [x] 阶段 7 连接池及双线程查询基本闭环：源码与错误路径专项通过，12/12 CTest；用户随后明确反馈新版 db_smoke 运行通过。实际文件尚无 CheckPrepared，不将本次反馈算作预处理语句通过。
- [x] 阶段 7 注册/密码验证后端：用户手工实现 AuthService、OpenSSL PBKDF2、预处理 INSERT/SELECT 及正式 HTTP 路由；12/12 CTest、独立业务/ET 网络专项及专项 ASan/UBSan 通过。用户随后执行真实 MySQL 脚本，确认 201/409/200/401 及重启后数据持久化，详见末尾记录。此功能不含登录会话和权限控制。
- [x] 阶段 7 页面：已提供注册/密码验证表单、原生 POST 降级和少量 JS 结果提示；随后将 CSS/JS 拆成外部静态资源并增加自生成 PNG。HTTP 加载和模拟 DOM 逻辑通过，未宣称真实浏览器视觉验收。
- [x] 静态资源基本单元：受限 /static/ GET、逐级 openat/O_NOFOLLOW、MIME、8 MiB 单文件限制及二进制读取实现；13/13 CTest、LT/ET 静态网络专项、读取模块 ASan/UBSan 和原有 Auth/网络回归通过，详见末尾记录。
- [ ] 后续进入可读性整理及收尾；堆替换扫描、异步日志等性能优化继续后置。
- [ ] 后续阶段待完成。

当前保留限制：只支持以 `/` 开头的请求目标、精确匹配 Content-Type、拒绝重复字段，不支持 chunked 或完整流水线。连接复用为显式 keep-alive 选择策略，非 HTTP/1.1 默认持久策略；Host 仅做存在、非空及重复检查。Socket 非阻塞，文件读取、响应构造已交给工作线程（工作线程内仍是同步文件 I/O）；已实现 30 秒网络无进展超时，连接数量上限及请求总耗时限制尚未实现；16 次请求上限与空闲超时用途不同，不代表生产级 HTTP 合规。

本项目使用独立 mywebserver 数据库及 users 表，不复用或修改参考项目表。数据库凭据由运行时环境提供，不写入源码或文档；账号只需应用表的 SELECT/INSERT 权限。

## 3. 技术选择与目录

- C++14、Linux Socket API、epoll、标准库线程设施。
- 使用 CMake，避免照搬参考项目不安全的清理规则。
- 数据库阶段使用 MySQL C API；初期不引入 Web 框架或大型测试框架。
- 测试初期使用简单断言测试程序和 curl，必要时增加短小的脚本。

目录按阶段逐渐建立，不预先生成空架构：

```text
MyWebServer/
├── AGENTS.md
├── docs/spec.md
├── CMakeLists.txt
├── code/
│   ├── main.cpp
│   ├── buffer/
│   ├── http/          # HttpRequest、HttpParser、表单解析、BuildResponse
│   ├── server/        # Connection、Epoller、Server
│   └── experiments/  # nonblocking_demo
├── tests/
├── resources/        # 优先使用自己写的小型页面
└── build/            # 构建产物，不提交
```

## 4. 对参考项目的初步审查

以下是现有源码的静态检查结论，不是完整审计。每个模块开始前仍需复查，并尽量用测试复现缺陷。

### 4.1 值得学习的设计

| 设计 | 评价与采用条件 |
| --- | --- |
| epoll + 非阻塞连接 | 适合 Linux 多连接服务；先掌握 LT，再学习 ET 的读写耗尽要求 |
| 主线程分发事件、线程池处理连接 | Reactor 风格，职责基本清晰；需要明确连接所有权和并发关闭规则 |
| 可增长 Buffer | 能减少固定缓冲区限制；仍必须限制请求大小，并正确维护读写下标 |
| HTTP 状态机 | 方向合理，但必须跨多次接收保存解析状态 |
| 小根堆定时器 | 适合管理连接超时；必须保证堆边界、节点删除和连接生命周期正确 |
| 数据库连接池 + RAII | 方向合理；连接必须在整个使用作用域内独占，并且只归还一次 |
| 阻塞队列 + 异步日志 | 有助于理解生产者—消费者；要考虑队列上限、关闭及后台线程回收 |
| mmap + writev | 可作为静态文件发送方式的练习，但不宣称严格零拷贝；先用简单发送实现正确性 |

### 4.2 已发现的问题与学习实现的处理方向

| 位置 | 问题 | 我们的处理方向 |
| --- | --- | --- |
| `build/Makefile`、`test/Makefile` | clean 规则危险拼接可能匹配源码 | 使用独立构建目录，不照搬规则 |
| `code/pool/sqlconnpool.cpp` | 建连失败仍将空指针入池；取连接前的队列检查也涉及并发访问 | 初始化失败显式报告并退出；同步管理队列、等待及关闭 |
| `code/http/httprequest.cpp` | RAII 临时对象在语句结束时归还连接，后续仍使用；又手工归还 | 有名的作用域 RAII 对象，禁止重复归还 |
| 同上 | SQL 字符串拼接、密码明文存储且写入日志 | 使用预处理语句、不记录密码；数据库阶段再选成熟密码哈希库 |
| 同上及 `httpconn.cpp` | 半包时解析状态、完整性判断不足，处理时重新初始化请求 | 跨读取保留状态，以 Content-Length 判断正文完成后再处理 |
| `code/timer/heaptimer.cpp` | 上浮的无符号下标可能下溢，`j >= 0` 不能构成有效边界 | 明确以 `i > 0` 为上浮条件，测试根节点、相等到期时间等情况 |
| `code/http/httpresponse.cpp` | mmap 错误检测先解引用返回指针 | 与 MAP_FAILED 直接比较，并测试空文件、映射失败和资源回收 |
| `code/server/webserver.cpp` | 设置状态标志时误用 F_GETFD 而非 F_GETFL | 使用 F_GETFL / F_SETFL 保留原状态标志 |
| 同上 | 静态目录依赖启动工作目录，追加路径受固定缓冲区限制 | 用 std::string 管理路径，后续加入显式资源目录配置 |
| 静态资源处理 | 直接拼接请求路径，没有充分约束目录穿越 | 明确请求路径校验和静态目录边界；测试 `..` 及编码绕过 |

线程池关闭、连接复用、定时器与工作线程并发关闭、日志并发和格式化边界尚需专项审查，不能因首页能打开就判定这些部分正确。

## 5. 分阶段实现与验收

每阶段采用：读相关代码与接口文档 → 记录设计选择 → 用户实现最小版本 → 测试 → 审查比较。默认不由 agent 一次性代写。

### 阶段 1：最小阻塞 TCP 服务端（已完成）

参考：`server/webserver.cpp` 的 `InitSocket_()`、`DealListen_()`；`man 2 socket/bind/listen/accept/close`、`man 7 ip`。

实现：
- 仅监听 `127.0.0.1:8080`，接受一个连接并打印提示，然后关闭。
- 检查每个调用的返回值；失败时回收已经获得的资源。
- 第一版可以直接用 g++ 编译，通过后再加入最小 CMake 配置。

验收：
- curl 连接时，服务端能打印连接提示并退出；此时 curl 空响应或连接重置不算 HTTP 功能失败，因为尚未实现 HTTP。
- 重复占用同一端口时给出清晰错误。
- 能解释监听 fd 与连接 fd 的区别、网络字节序、阻塞 accept。

### 阶段 2：固定 HTTP 响应与简单静态文件

参考：`http/httpconn.cpp`、`http/httpresponse.cpp`；先理解 HTTP 请求行、头部和空行分隔。

实现：
- 先观察 recv 得到的原始请求，再返回固定 Hello World。
- 正确生成状态行、Content-Type、以字节计数的 Content-Length、CRLF。
- 实现发送全部数据的循环，处理部分发送和 EINTR，避免断连触发 SIGPIPE 导致进程退出。
- 先串行处理多个连接，每次响应后关闭连接。
- 随后支持一个自写首页、404、空文件；明确此阶段的临时协议限制。

验收：
- 浏览器和 `curl --noproxy '*' -i http://127.0.0.1:8080/` 可看到预期响应。
- 连续多个请求均可处理，不存在的资源返回 404，客户端中途断开不使服务端退出。

### 阶段 3：Buffer 与增量 HTTP 解析

参考：`buffer/`、`http/httprequest.*`，重点对比半包处理方式。

实现：
- 用 Buffer 分离接收数据和解析过程，保留未消费字节。
- 区分“数据不完整”“解析失败”“请求完成”，不把三者混为 bool。
- 最小支持 GET，以及带 Content-Length 的表单 POST；大小写不敏感地处理头字段名。
- 限制请求行、头部和正文大小；非法或不支持的请求明确拒绝并关闭。
- 不支持 chunked 请求体、流水线等完整 HTTP 特性，不静默误解析。

验收：
- 请求行、头部、正文分别拆成多次发送仍可解析。
- 多余字节不会被误当作本次正文；超大、无效长度和不支持的传输编码被拒绝。
- Buffer 的扩容、复用、边界有可重复的小测试。

### 阶段 4：单线程非阻塞 epoll（LT / ET 与最小 Keep-Alive 已验收）

参考：`server/epoller.*`、`webserver.cpp`。

当前落地：
- `code/main.cpp` 仅创建 Server 并调用 Init/run；socket/bind/listen、O_NONBLOCK、连接表及 LT 事件循环已迁移到 `code/server/server.cpp`，epoll 操作封装在 `epoller.cpp`。
- `code/http/httphandler.h/.cpp` 的 BuildResponse 已承接错误响应、首页、/echo、/form 路由及响应拼装。
- `code/server/connection.h` 为每个 fd 保存 input、parser、output、sent、responding；不要在每次事件中重置这些状态。
- 接收阶段关注 EPOLLIN | EPOLLRDHUP，生成响应后关注 EPOLLOUT，按构造参数保留 EPOLLET。Server 默认 LT，正式 main 为 ET。响应发送完成后按策略关闭或重置单次请求状态并继续接收；已记录半关闭时取消对 RDHUP 的持续关注，避免 LT 慢发送期间空转。
- 最新版本构建及 6 项 CTest 通过，完整 LT/ET 对照网络验收通过；覆盖情况及命令见末尾记录。

当前任务：阶段 4 当前范围已验收，进入阶段 5 的独立线程池单元；暂不把客户端 fd、Connection 引用或 epoll 事件处理交给工作线程，不加入数据库。

实现：
- 先 LT，维护每条连接的缓冲区、解析状态和待发送偏移。
- 正确处理 EAGAIN、部分读写和断连，按需注册 EPOLLOUT。
- LT 稳定后再尝试 ET，理解为什么 accept/read/write 需要循环至 EAGAIN。
- 最后增加最小 Keep-Alive 支持，顺序处理连接上的请求，不宣称支持完整流水线。

验收：
- 一个慢客户端不会阻塞其他连接；写出大响应时不会丢失或重复数据。
- 断连后不残留连接资源；LT 与 ET 的测试结果一致。

### 阶段 5：线程池及连接生命周期

参考：`pool/threadpool.h`、`webserver.cpp` 的事件分发。

实现：
- 先独立实现有界任务队列、条件变量和可 join 的工作线程。
- 明确停止接收任务、处理已排队任务、回收线程的顺序。
- 与服务器集成前，确定同一连接的串行处理和关闭协议；使用 EPOLLONESHOT 或其他明确机制。
- 防止 fd 复用导致旧任务操作新连接；不只靠裸指针约定。

验收：
- 小任务测试中不丢任务、不重复执行；析构时能回收线程。
- 客户端断连与工作任务同时发生时，无悬空访问；队列满时行为明确。

### 阶段 6：超时管理和基础日志

参考：`timer/`、`log/`。

实现：
- 根据用户对调用层次过多的反馈，先实现连接无进展超时：Connection 保存 steady_clock 的最后成功 I/O 时间，事件线程周期扫描并统一 CloseClient，覆盖接收、等待后台任务、发送、Keep-Alive 空闲。无需新线程、timerfd 或回调类。
- 首版默认 30 秒、扫描周期 1 秒；只在接受连接及 recv/send 正数返回时更新活动时间，EAGAIN、重复就绪和后台结果入队不算网络进展。超时关闭 socket 不代表取消已经运行的后台任务，仍使用 id 校验丢弃晚到结果。
- 扫描放在事件批次处理后，避免先关闭超时 fd 再误用同批旧事件；Wait 使用有限超时，EINTR 路径也检查过期。扫描频率节流为约每秒一次，复杂度 O(N)，不宣称适合超大连接量；事件线程繁忙时截止时间可能延后。
- 小根堆定时器及增删改测试作为后期优化待办，使用单调时钟；按用户要求不在当前功能主线中立即替换扫描。
- 超时关闭与线程任务遵循同一生命周期协议，关闭时注销对应定时器。
- 先同步日志：时间、级别、固定事件名、fd/id、错误号；记录接受、关闭、超时、任务拒绝和构造失败，不记录原始请求/正文/凭据。先输出 stderr，可由运行环境重定向，不引入单例、宏、轮转或新线程。
- 有界异步日志队列及写线程后置为性能优化学习项，不为追求异步而打断基础功能主线。
- 日志不记录密码；错误输出不依赖尚未初始化的日志系统。

验收：
- 空闲连接超时关闭，活动连接续期正常。
- 根节点、相同到期时间、删除后复用连接等情况有测试。
- 退出时后台线程可回收，日志不越界；队列满时行为有文档说明。

### 阶段 7：MySQL 与简单注册登录

参考：`pool/sqlconn*`、`HttpRequest::UserVerify()`。

实现：
- 先验证单连接查询，再写连接池和有名的 RAII 借用对象。
- 使用独立低权限账号、环境变量提供凭据；数据库不可用时明确启动失败。
- 使用预处理语句，用户名唯一约束，正确传播插入失败。
- 选择成熟密码哈希库后再存储密码，不自制算法；依赖选择需先确认。
- 提供简单注册、密码校验页面；不扩展为完整会话或权限系统。

验收：
- 正确密码、错误密码、不存在用户、重复注册都返回正确结果。
- 含引号输入不会改变 SQL 结构；数据库断连不会被错误报告为注册成功。
- 借出与归还严格配对，凭据和密码不进入日志及版本库。

### 阶段 8：收尾与小规模性能观察

#### 可读性整理（明确待办，功能基线稳定后实施）

- 目标是让请求路径易追踪，不是增加更多类或把每几行拆成函数。并发专项通过后可先整理命名，超时/日志接入后集中做一次行为不变的结构整理。
- 将 processing/responding 两个布尔量统一为 Reading/Processing/Writing 状态，减少非法组合；集中标注状态转移及对应事件掩码。
- PrepareResponse 当前实际是解析并提交任务，后期改为能反映异步职责的命名；按初始化、事件分发、读、提交、完成、写、关闭的调用顺序组织实现。
- 仅在确实减少重复与跳转时抽取事件掩码或完成通知辅助代码，不引入通用框架、多层虚函数或额外回调链。
- 添加一页调用图、方法职责/线程归属表，标明哪个入口改变哪些状态、返回值表示什么。保留“网络和连接表只归事件线程”的边界。
- 验收：用户能沿一个请求定位读取、提交、完成、发送、关闭的代码；重构前后 LT/ET、Keep-Alive、生命周期及并发专项结果一致。不与新增业务功能在同一批改动混做。

#### 最终回归与观察

- 整理构建、启动、支持的协议范围和已知限制。
- 回归静态文件、HTTP、并发、定时器和数据库测试。
- 使用 ASan/UBSan 检查内存问题；必要时单独使用 TSan 检查并发。
- 在用户确认后，做小规模本机压测，记录并发数、请求内容、耗时、错误率、CPU/内存和构建参数。
- 可选比较普通文件读取与 mmap + writev；没有测量收益就不以“高性能”名义增加复杂度。

## 6. 比原项目多做什么：只加两项小改进

正确性修复、输入边界和凭据保护属于基础要求，不当作额外卖点。完成基线后只增加：

### A. 最小启动配置

支持 `--port`、`--root` 两个参数；数据库凭据从环境变量读取。不要写通用配置框架。

验收：换端口、换启动目录仍能访问指定资源目录；非法参数有清晰报错。默认仍绑定本机地址。

### B. 一个轻量健康检查

增加 `GET /health`，固定返回 200 和简单 JSON，不暴露数据库账号、密码或系统敏感信息。

验收：能用 curl 或小脚本判断 HTTP 服务存活。它只表示服务可响应，不等同于数据库健康或完整业务可用。

不再扩展监控后台、复杂路由、TLS、WebSocket、HTTP/2、缓存系统、分布式能力或完整用户系统。

## 7. 每阶段的学习记录

阶段结束时在本文末尾追加简短记录：

```text
阶段：
实现范围：
为什么这样设计：
参考项目哪里合理、哪里没采用：
验证命令与实际结果：
仍存在的限制：
下一小步：
```

不要仅凭“代码写完了”标记完成。阶段 4 LT 子单元已完成源码审查与网络验收；下一步见末尾记录。其他阶段可以根据学习反馈收缩，不要求一次规划就全部实现。

### 阶段 3 收尾记录

- 已实现 Buffer、HttpRequest、HttpParser 状态机、正文长度与上限检查、文本回显和 URL 编码表单接口。
- 已修复接入时的 EOF 未标记失败、BodyTooLarge 未映射 413、成功解析遗漏返回值等问题。
- agent 实际执行 CMake 构建、4 项 CTest、6 份 Python 网络脚本及严格请求行/控制字符网络检查，全部通过；临时服务已回收。
- 合理设计沿用状态机与缓冲区分层；未照搬原项目的半包处理和 URL 解码缺陷。
- 后续进展：非阻塞实验与 LT 初版已经写出，具体状态见下方阶段 4 交接记录。

### 阶段 4 初版交接记录（历史，验收结果见下一节）

- 用户已理解/接触：O_NONBLOCK、F_GETFL/F_SETFL、EPOLLIN/EPOLLOUT、ADD/MOD/DEL、EINTR/EAGAIN、半关闭，以及按位或/按位与；仍需要结合真实执行路径讲解，不能假定已熟练。
- 用户已写出 LT epoll 版本；main 中 epoll 描述符变量名为 epbf（只是命名，不必无关重命名）。BuildResponse 和 Connection 已迁移，不存在此前的 // xxx 待填中间状态。
- agent 在本次交接时执行 `cmake --build MyWebServer/build` 和 `ctest --test-dir MyWebServer/build --output-on-failure`：构建成功，4/4 测试通过。
- 历史 6 份 Python 网络脚本是在此前串行版本执行通过；不能沿用该结论声称最新 LT 版本已完成网络验收。
- 下一轮由 agent 执行：现有 split_request.py、header_validation.py、bad_request.py、body_request.py、framing_validation.py、form_request.py，再补慢请求不阻塞其他连接、完整请求后半关闭、RST、慢接收/部分发送及连接 fd 回收检查。
- 测试前检查 8080 是否被占用；不要结束用户原有进程。临时测试服务器从 MyWebServer 根目录启动，测试结束回收。
- 慢发送测试要验证一个连接只发送半个请求头/正文时，另一个连接仍可完成响应，随后补齐慢连接也能正确回复。
- 部分发送测试不能只靠小响应成功就宣称覆盖 send 的 EAGAIN；若未能可靠触发，明确标记待验证。若使用临时大资源或专用测试环境，先说明方案并避免破坏首页。
- 优先审查：事件组合与半关闭、关闭后是否继续使用 conn 引用、EAGAIN 是否保留状态、按需关注 EPOLLOUT、防止错误结果落入首页分支、资源清理。确认 LT 后，再决定封装 Epoller/服务器对象、尝试 ET 和 Keep-Alive 的顺序。

### 阶段 4 LT 子单元验收记录

实现范围与审查：
- 审查实际 main.cpp、Connection、BuildResponse、HttpParser 及相关 Buffer / HttpRequest；没有重写服务器，也未发现本轮必须修复的服务器缺陷。
- 每次 LT 事件只 recv/send 一次是合法策略：条件仍成立时 epoll 会再次通知。NeedMore / EAGAIN 保留每连接状态；生成响应后才订阅 EPOLLOUT，发送成功后按实际返回字节数推进 sent。
- closeClient 的调用路径未发现关闭后继续使用 conn；半关闭不是立即丢弃已收到请求的理由。与参考项目相比继续沿用非阻塞 + epoll 机制，不引入其线程池/数据库或 ET 组合。

新增可复用测试：
- `tests/lt_network.py`：自管服务器进程、检查端口、执行现有六份脚本及补充验收；临时 8 MiB 首页放在隔离目录，不修改用户首页。启动失败或断言失败也会回收本次子进程。
- `tests/send_probe.c`：只通过 LD_PRELOAD 作用于专用测试进程，不链接到服务器。缩小发送缓冲区以记录真实部分发送，并在零偏移/已有发送进度时各注入一次 EAGAIN。

实际执行命令（从工作目录 `/home/jason/study/other_cprj`）：

```sh
cmake --build MyWebServer/build
ctest --test-dir MyWebServer/build --output-on-failure
python3 MyWebServer/tests/lt_network.py
cc -shared -fPIC -Wall -Wextra MyWebServer/tests/send_probe.c -ldl -o MyWebServer/build/send_probe.so
python3 MyWebServer/tests/lt_network.py --send-probe MyWebServer/build/send_probe.so
```

实际结果：
- 构建及 4/4 CTest 通过；六份原有网络脚本在最新 LT 上通过（不是沿用串行版历史结果）。
- A 仅发送部分请求头/正文时不被提前响应，B 可在 A 未补齐前完成；A 补齐后也成功。
- 完整 GET 和 64 KiB 二进制 echo 请求后 SHUT_WR，均得到完整响应；不完整请求头/正文后 EOF 则关闭，不返回成功响应。
- 40 个顺序异常/空连接、发送中 RST 后服务仍可响应；通过 `/proc/<pid>/fd` 检查 fd 回到 5 个基线。未做高并发压测。
- 无探针和有探针两种情况下，8 MiB 响应均按 Content-Length 和完整正文逐字节比较通过；慢接收期间另一连接可完成 404 请求；发送完成/发送中 RST 后 fd 回收。
- 最终探针运行记录 134 次真实部分发送，2 次注入 EAGAIN；第一次请求发送 46336 字节后，第二次注入时剩余 8342375 字节，最终数据不丢失、不重复。
- 自然触发的内核 send EAGAIN 为 0 次；本轮验证的是注入后的恢复路径，不宣称自然触发过。未定向强制 recv EAGAIN、EINTR 或 epoll_ctl 失败，也未做 ASan/内存泄漏专项检查；fd 恢复不等于所有内存泄漏均已排除。
- 本次服务进程及临时资源全部回收，结束后 8080 无监听；未修改参考项目或服务器核心实现。

仍存在的限制：同步文件读取/响应构造仍可能拖慢事件循环；没有超时及连接数上限；仍是一请求一响应后关闭，不支持 ET / Keep-Alive。慢网络客户端隔离通过不意味着磁盘 I/O 也已异步化。

本节验收后的计划是封装最小 Epoller / Server；该单元现已完成，结果见下一节。

### 阶段 4 Epoller / Server 封装验收记录

实现范围与设计：
- 用户实现 `Epoller` 的 init/ADD/MOD/DEL/Wait/Event，以及 `Server` 的 Init/run/AcceptClients/HandlerClientEvent/CloseClient；main 只保留启动入口。保持单线程、非阻塞 LT、Connection: close。
- Epoller 只拥有 epoll fd，不拥有注册进去的 socket；Server 负责监听及客户端 socket。禁止复制资源所有者，析构覆盖未初始化、初始化失败及正常离开作用域。
- 已审查并确认此前的遗漏已由用户修正：注册前创建 epoll、有效 fd 判断使用 >= 0、Init 失败不重复关闭成员 fd、事件循环下标符号一致。
- 与参考项目相比采用小型事件封装，但不依赖 assert 处理 epoll 创建失败，不允许复制 fd 所有者，不引入其线程池/数据库。agent 本轮未修改服务器核心实现。

新增/调整的测试：
- `tests/server_lifecycle_test.cpp` 与 CMake 测试目标：socketpair 验证 ADD/MOD/DEL、事件 fd/标志、超时、重复注册/无效 fd/未注册 fd 的真实错误返回；验证 Epoller 析构不关闭被观察的 socket。
- 验证端口占用时 Server 初始化失败且不影响占用者、重复 Init、十轮初始化/析构、监听 fd 为 0 时的析构、真实 EMFILE 下的 socket/epoll 创建失败回收。端口占用者由测试创建，测试前要求 8080 空闲。
- 通过仅链接到测试程序的 `--wrap=epoll_wait`，在 accept 一个连接后注入 EIO；确认 run 返回 1，离开作用域时关闭监听、epoll 及仍存活的客户端连接，对端收到 EOF。该 EIO 为故障注入，不是自然内核故障。
- CTest 增至 5 项；生命周期测试使用 RUN_SERIAL、10 秒超时，并显式保留断言。需要空闲 8080，外部服务不受 CTest 串行设置控制。
- LT 网络测试首次遇到一次固定延时后的 fd 数量断言失败，单独重跑通过；不能据此认定服务器缺陷。测试已改为完成一次 404 请求后才判定就绪，且在慢接收 socket 握手前限制 SO_RCVBUF，消除未处理启动探针及接收自动调优带来的测试不确定性。修正后完整网络测试通过，大响应组另连续执行三轮通过；未改服务器来迁就测试。

实际执行命令（在 `MyWebServer/` 下）：

```sh
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure
python3 tests/lt_network.py
cc -shared -fPIC -Wall -Wextra tests/send_probe.c -ldl -o build/send_probe.so
python3 tests/lt_network.py --send-probe build/send_probe.so
```

实际结果：
- 构建成功，5/5 CTest 通过；六份原有脚本在本次封装后的二进制上通过。
- 慢请求头/正文隔离、完整请求后半关闭（含 64 KiB echo）、截断 EOF、RST、8 MiB 完整正文比较、慢接收隔离、发送中 RST 和 fd 恢复基线均通过。
- 本次最终发送探针记录 181 次真实部分发送、2 次注入 EAGAIN、0 次自然内核发送 EAGAIN；注入位置包含 sent 为 0 和已有发送进度两种情况。
- 测试启动的服务及临时资源均已回收，最终 8080 无监听。用户首页、参考项目和用户其他修改未动。

覆盖边界：
- 未做 ASan/内存泄漏专项、自然发送 EAGAIN、定向 recv EAGAIN/EINTR 或完整系统调用失败矩阵；不能将 fd 恢复等同于所有内存问题均已排除。
- Ctrl+C/SIGTERM 默认终止进程时不会正常执行局部 C++ 对象析构；本轮用独立作用域及注入 run 错误返回验证析构，不宣称已实现优雅停机。
- 同步文件读取/响应构造、缺少超时/连接上限等限制未改变；ET、Keep-Alive 尚未实现。阶段 4 整体尚未完成。

本节完成后的 ET 计划现已实施，结果见下一节；本节的 5 项 CTest 及未观察自然 EAGAIN 等结论仅代表当时版本。

### 阶段 4 ET 子单元验收记录

实现范围与原因：
- 用户增加 Server 模式选择、TriggerMode 和三处事件掩码；接收逐块解析至 EAGAIN 或请求终结，发送按 sent 循环至 EAGAIN 或全部完成。ET 不保证持续就绪但未处理完的 fd 被重复通知，因此不能只添加 EPOLLET 而保留单次 I/O。
- 当前 main 为 ET；默认构造仍为 LT。共用协议逻辑和读写循环，没有新增 Keep-Alive、EPOLLONESHOT、线程池或数据库。
- 与参考项目相比保留非阻塞/ET 机制，但不引入其并发组合，也不照搬 RDHUP 立即关闭等路径。

审查发现及最小修复：
- 首次构建及 5 项旧 CTest、六份小请求脚本通过，但 8 MiB 慢接收失败；服务日志为 send: Resource temporarily unavailable，连接 fd 提前回到基线。
- 发送分支误写成“非 EAGAIN 错误 continue”：真实 EAGAIN 落入关闭路径，而其他永久错误可能无限重试。agent 改为 EAGAIN/EWOULDBLOCK 立即返回事件循环，保留 output/sent；其他失败继续关闭。
- 接收判断从 EAGAIN && EWOULDBLOCK 改为 ||。Linux 两值相同使旧写法未在本机暴露，但逻辑应是任一个均可。
- 对 EPOLLET 做显式 uint32_t 转换，消除条件表达式类型警告。除此之外未重写用户核心实现。

测试适配：
- 新增 `tests/lt_server_main.cpp`、CMake 目标 `server_lt_test`，复用正式 Server 源码，仅选择 LT，避免来回修改正式 main。
- 生命周期程序支持 ET 参数，新增 `server_lifecycle_et`，CTest 共 6 项。
- `tests/lt_network.py` 保留文件名以兼容已有命令，新增 --server、--mode 和 --observe-send。通过 /proc 中 epoll fdinfo 检查内核实际注册的触发标志，覆盖监听、半请求读取和慢响应发送期间。
- 新增不带 FIN 的单次 64 KiB echo、5 个少量同时连接验证；不是高并发压测，也不是确定性地控制 accept 队列在某次通知前已全部排好。
- send_probe 新增接收 EAGAIN 记录，ET 使用 SEND_PROBE_OBSERVE=1 禁止人工 EAGAIN，只缩小发送缓冲区并记录真实系统调用结果。原因是虚假 EAGAIN 不一定伴随真实的就绪变化，原 LT 注入方式不适合直接用于 ET。

实际执行命令（在 MyWebServer/ 下；测试前 8080 空闲）：

```sh
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure
cc -shared -fPIC -Wall -Wextra tests/send_probe.c -ldl -o build/send_probe.so
python3 tests/lt_network.py --mode ET --observe-send --send-probe build/send_probe.so
python3 tests/lt_network.py --mode LT --server build/server_lt_test --send-probe build/send_probe.so
python3 tests/lt_network.py --mode ET
python3 tests/lt_network.py --mode LT --server build/server_lt_test
```

实际结果：
- 修复后构建无本轮类型警告，6/6 CTest 通过。LT、ET 在有探针和无探针两种情况下均通过完整网络回归。
- 六份原有脚本、慢请求头/正文隔离、64 KiB 单次提交及半关闭、截断 EOF、少量同时连接、RST、8 MiB 正文逐字节比较、慢接收隔离及连接 fd 回到 5 个基线均通过。
- ET 探针：23 次真实 recv EAGAIN、181 次真实部分发送、181 次真实 send EAGAIN、0 次人工注入；读/写暂停后恢复成功。
- LT 探针：20 次真实 recv EAGAIN、180 次真实部分发送、179 次真实 send EAGAIN、2 次人工注入；恢复成功。次数受调度影响，不是性能指标。
- 本轮测试子进程及临时目录全部回收，最终 8080 无监听；原首页和参考项目未修改。

结论与边界：
- ET 子单元验收通过，LT 基线行为未破坏；没有进行性能基准，因此不宣称 ET 比 LT 更快。
- LT 也可以循环到 EAGAIN；ET 也不要求请求终结后继续读取无关请求。当前完整请求即切换响应，保留半关闭语义。
- 单连接耗尽循环可能长时间占用事件线程；ET 若要加入单轮预算，需要额外安排剩余任务，不能提前退出后盲等新边沿。本轮未实现公平调度。
- 未强制 EINTR、未定向注入永久发送错误、未跑 ASan/内存泄漏专项或高并发压测。同步文件 I/O、超时/连接上限、优雅停机和 HTTP 范围限制仍在。
- 本节完成后的 Keep-Alive 单元现已实现，验收见下一节。

### 阶段 4 最小 Keep-Alive 验收记录

实现范围及设计：
- 客户端显式 Connection: keep-alive 时允许复用，token 大小写不敏感、支持逗号列表，close 优先；默认仍关闭。每个连接最多 16 次请求；解析失败或观察到对端半关闭则响应后关闭。
- HandlerClientEvent 负责网络读写和连接状态流转，PrepareResponse 负责解析当前 Buffer、决定复用策略、构造 output 并切换写事件；true 只表示连接有效，不能当成请求完整。
- 发送完成后重置 parser/output/sent/responding，不清除 input、请求计数或整个 Connection；先尝试解析已缓存的后续字节，再决定关注读事件，避免 ET 等不到用户态数据的通知。
- 能串行处理提前到达的后续请求字节，但不宣称支持完整 HTTP 流水线能力，没有并发响应或多响应队列。没有新增线程池、数据库或超时机制。

本次发现与必要修复：
- 初次构建及 6 项 CTest 通过，但 HTTP 启动探针收到空响应。PrepareResponse 遗漏 BuildResponse 调用，output 为空使发送阶段立即关闭；agent 补齐 output 赋值。
- 事件处理遗漏 peerWriteClosed 标记；agent 补齐对 RDHUP/HUP 的记录，保留完整请求后半关闭仍回复的路径。
- 补测对端半关闭但仍慢接收大响应时，LT 会因持续关注已就绪的 RDHUP 而反复唤醒。新增 fdinfo 验证复现订阅未移除的问题后，改为记录半关闭后取消 RDHUP 关注；准备响应时已知半关闭则不注册 RDHUP。仍保留写关注，直到发完响应。

验证与实际结果：
- 新增 `tests/keepalive_network.py`，按 Content-Length 读取并保留多余响应字节，复用已有测试进程管理。
- 构建、6/6 CTest 通过；原有六份网络脚本及完整 LT/ET 回归均通过。
- LT、ET 的 Keep-Alive 专项均在有探针、无探针环境通过：同一 socket inode 连续 echo/form/404、64 KiB 二进制正文、旧请求字段不泄漏、默认关闭、大小写/token 列表、close 优先、16 次上限。
- 一次提交多个请求及部分下一请求后再补齐、解析错误/超限关闭、复用后正常关闭/RST、8 MiB 慢响应后同连接继续请求、半关闭大响应后 EOF 均通过；连接资源回到 5 fd 基线。
- 半关闭大响应检查 epoll fdinfo 确认已不关注 RDHUP，避免依赖 CPU 测量猜测是否忙循环；未做性能基准。
- 最终 Keep-Alive 专项探针在 LT、ET 各记录 358 次真实部分发送、358 次真实 send EAGAIN，0 次人工注入；包括正常复用和半关闭两份 8 MiB 响应。
- 默认关闭回归：ET 记录 24 次 recv EAGAIN、180 次真实部分发送/send EAGAIN，0 次注入；LT 为 24 次 recv EAGAIN、181 次部分发送、180 次真实 send EAGAIN、2 次注入。次数不代表性能。
- 所有测试子进程和临时资源已回收，8080 无监听；参考项目和原首页未修改。

实际执行命令（MyWebServer/ 下，8080 需空闲）：

```sh
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure
cc -shared -fPIC -Wall -Wextra tests/send_probe.c -ldl -o build/send_probe.so
python3 tests/keepalive_network.py --mode ET --send-probe build/send_probe.so
python3 tests/keepalive_network.py --mode LT --server build/server_lt_test --send-probe build/send_probe.so
python3 tests/keepalive_network.py --mode ET
python3 tests/keepalive_network.py --mode LT --server build/server_lt_test
python3 tests/lt_network.py --mode ET --observe-send --send-probe build/send_probe.so
python3 tests/lt_network.py --mode LT --server build/server_lt_test --send-probe build/send_probe.so
```

限制与下一完整单元：
- 仍无空闲超时、连接数上限、优雅停机、ASan/内存泄漏专项或完整 HTTP 合规；提前排队多个请求后再半关闭的完整流水线语义不在验收承诺内。
- 下一单元为阶段 5 的独立有界线程池：固定工作线程、Submit 非阻塞拒绝满队列、Close 停止接收且排空已接收任务、析构 join、任务异常隔离及部分建线程失败回滚。用户核心实现，agent 测试。
- 已审查参考 `pool/threadpool.h`：采用任务队列、互斥锁及条件变量；不采用 detached 工作线程、无界队列、关闭后仍可入队、不能等待工作线程退出等设计。其 Pool 经 make_shared<Pool>() 值初始化，不能仅看到 bool 无类内初值就直接断言初值不确定。
- 本单元不立即接入 Server。先验收任务执行与线程退出，再单独设计同连接串行处理、fd 复用和跨线程生命周期，不能直接把裸 Connection 引用放入任务队列。

### 阶段 5 独立有界线程池验收记录

实现与检查：
- 用户实现 `code/pool/threadpool.h/.cpp` 及 CMake 库目标，固定工作线程、有界等待队列、Submit 非阻塞拒绝、关闭后排空、异常隔离和析构 join。
- 首次阅读存在构造函数多余逗号、Close 误写成 join、JoinWorks 缺少定义；用户在检查期间继续修正。实际构建时逗号已修正，静态库可生成，但加入测试程序后真实复现缺少 JoinWorks 的链接错误。
- 用户随后补齐 JoinWorks 以及 Close 的加锁置 closing_ 和 notify_all。agent 重新读取并保留最新修改，未覆盖并发编辑；仅调整构造函数缩进/循环花括号，消除误导性缩进警告。之前数次精确替换因文件变化未匹配，均未写入对应修复。
- 本轮未把线程池接入 Server，未修改网络数据流。

新增测试与实际结果：
- 新增 `tests/threadpool_test.cpp`、CMake/CTest 的 threadpool_basic，设置 20 秒超时并启用断言。
- 无效参数、空任务拒绝、空池十轮销毁通过。
- 用 promise/future 控制任务阻塞与启动，确定性验证运行任务不占等待队列容量、满队列拒绝、Close 幂等、拒绝关闭后提交、排空已接收任务。
- 三个工作线程同时进入阻塞任务，确认任务不在队列锁内执行；四提交线程/四工作线程的 400 个任务各执行一次，通过。
- Submit 与 Close 并发时，所有成功接收的任务最终执行一次；单任务抛异常后后续任务仍执行；析构等待未完成任务，通过。
- 构建无本轮警告，7/7 CTest 通过；threadpool_basic 连续重复 20 轮通过。
- 独立编译并运行线程池 ASan/UBSan（启用泄漏检测）通过，无 sanitizer 报告。此结论只覆盖独立线程池测试，不代表 HTTP 服务器整体完成 sanitizer 验收。

实际执行命令（MyWebServer/ 下）：

```sh
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure
ctest --test-dir build -R '^threadpool_basic$' --repeat until-fail:20 --output-on-failure
g++ -std=c++14 -g -O1 -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer tests/threadpool_test.cpp code/pool/threadpool.cpp -pthread -o build/threadpool_test_asan
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 timeout 20s ./build/threadpool_test_asan
```

边界与下一单元：
- 未执行 TSan，ASan/UBSan 和重复运行不能证明没有所有数据竞争；未定向注入部分建线程失败或内存分配失败，仅审查构造回滚路径。
- 对象析构期间不允许外部继续访问，任务不能销毁其所属池；任务若永不结束，析构也会一直等待。任务异常只记录，不通过 future 传播给提交方。
- 本轮未重复执行 HTTP 网络脚本：服务器未接入线程池，网络源码未改；旧 HTTP 验收结论与本轮线程池验收分别记录。
- 下一完整单元直接进入线程池接入与连接生命周期，不再拆成多个线程小实验。需要确定事件线程/工作线程职责、同连接独占、任务拒绝策略、完成通知以及断连/fd 复用后的旧任务处理，再由用户实现并进行网络并发验收。
- 按完整 spec 范围粗估当前进度约 55%～60%，非精确工时比例；这是独立线程池验收时的记录，接入进展见下一节。

### 阶段 5 线程池接入：已迁移，常规回归通过，专项待验收

本轮实现与授权范围：
- 用户已添加 processing/连接 id、请求副本提交、完成队列、eventfd、InitWorkers、通知分发与析构顺序，并明确要求 agent 直接替换 HandlerClientEvent 及之后的剩余迁移。
- agent 修改 HandlerClientEvent：后台处理时停止接收；RDHUP/HUP 更新/取消持续关注；提交后按 processing 返回；Keep-Alive 的缓存后续请求提交后等待结果，不恢复读关注。
- PrepareResponse 原先残留同步 BuildResponse、responding=true 和 EPOLLOUT 注册，又额外提交后台任务；agent 移除同步构造路径，改为 processing=true/responding=false，暂停普通读写，只提交一次任务。
- run 的 eventfd 分发已由用户写好并保留。CMake 为 server、server_lt_test、server_lifecycle_test 链接 threadpool。
- 工作线程只处理 HttpRequest 副本并发布结果，事件线程检查 fd/id、更新连接、注册写事件并执行 send。CloseClient 及连接表仍只由事件线程操作。

本轮实际验证：
- 新增一个 eventfd 后，空闲服务资源为 stdin/stdout/stderr、listen、epoll、eventfd，共 6 个；调整生命周期测试的 +3/+5 预期以及网络脚本基线常量，不以旧的 5 fd 断言误判为泄漏。
- 构建及 7/7 CTest 通过；完整 LT/ET 网络回归各执行一次，包含原有六份脚本、慢请求隔离、64 KiB 请求、半关闭、RST、8 MiB 内容比较及 fd 回收。
- LT/ET Keep-Alive 专项各执行一次，同一连接多请求、16 次上限、缓存后续请求、错误关闭、大响应复用及半关闭慢接收通过。
- ET 默认关闭探针记录 19 次真实 recv EAGAIN、181 次真实部分发送/send EAGAIN、0 次注入；LT 为 18 次 recv EAGAIN、180 次部分发送、179 次 send EAGAIN、2 次注入。两种模式的 Keep-Alive 专项各记录 358 次真实部分发送/send EAGAIN，0 次注入。
- RST 回归中出现一次预期的 recv: Connection reset by peer 日志，服务继续工作并回收连接；不是整个测试失败。
- 本次服务进程及临时资源已回收，8080 无监听。未修改参考项目或原首页。

执行命令（MyWebServer/ 下，8080 需空闲）：

```sh
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure
cc -shared -fPIC -Wall -Wextra tests/send_probe.c -ldl -o build/send_probe.so
python3 tests/lt_network.py --mode ET --observe-send --send-probe build/send_probe.so
python3 tests/keepalive_network.py --mode ET --send-probe build/send_probe.so
python3 tests/lt_network.py --mode LT --server build/server_lt_test --send-probe build/send_probe.so
python3 tests/keepalive_network.py --mode LT --server build/server_lt_test --send-probe build/send_probe.so
```

尚未验证，不能提前标记完成：
- 当前回归不等于定向验证了所有多线程边界。下一步集中补慢 BuildResponse 不阻塞事件线程、确定性的队列饱和/拒绝、同连接不重复提交、断连时后台仍运行、fd 被复用后丢弃旧结果、待处理任务存在时的退出回收。
- 普通 7 项 CTest 的析构路径尚未强制制造“Server 正在处理后台响应任务”的情形；独立池的 join 测试不能代替该集成测试。
- 本轮未对集成服务器运行 TSan/ASan，未对 PublishCompletion 的低内存/通知不可恢复失败进行故障注入。该函数仍为 noexcept，致命错误终止策略未扩展为恢复机制。
- 本节记录的是常规回归刚完成时的状态；后续专项已完成，见下一节。

### 阶段 5 线程池接入专项验收与下一单元

本轮变更：
- 未修改服务器核心实现。新增 `tests/server_async_test.cpp` 和两个 CTest：server_async_lt/server_async_et。
- 测试使用真实 Server、线程池、epoll、eventfd 和非阻塞 socketpair；仅测试目标启用 -fno-access-control 以检查状态，并用可控 BuildResponse 替身暂停/放行任务，不改变正式 handler 或公开测试入口。
- 队列饱和使用测试内的 2 线程、2 等待位置，避免以大量连接压测制造概率性结果。正式配置仍为 4 线程、64 等待位置。

实际通过：
- 慢构造屏障未释放时，另一连接完成响应，且 BuildResponse 确认运行在非事件线程；重复提交同一连接的就绪事件不重复构造。
- 两运行任务、两等待任务时，第五次提交被关闭拒绝，没有在事件线程同步兜底；已有任务释放后均正确完成。
- 用受控 HUP 事件删除后台处理中连接，强制复用相同 fd 且新 id 不同；在新连接 processing 状态下注入旧 id 完成结果，验证确实被过滤；随后两个真实后台结果返回，新连接内容正确。
- 构造替身抛异常后连接关闭，线程池仍可执行后续请求。
- Server 析构时存在运行任务及排队任务，析构等待屏障释放、任务完成后才返回；各组结束后 fd 恢复基线。
- 9/9 CTest 通过，LT/ET 新专项各连续重复 10 轮通过；集成专项 ASan/UBSan 在两种模式下通过（包含泄漏检测）。
- 另对正式 HTTP handler 的服务器执行 LT/ET 全部原网络与 Keep-Alive 回归，无探针环境均通过。测试进程均回收，最终 8080 无监听。

实际执行命令（MyWebServer/ 下）：

```sh
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure
ctest --test-dir build -R '^server_async_' --repeat until-fail:10 --output-on-failure
g++ -std=c++14 -g -O1 -Wall -Wextra -UNDEBUG -fno-access-control -fsanitize=address,undefined -fno-omit-frame-pointer tests/server_async_test.cpp code/server/server.cpp code/server/epoller.cpp code/pool/threadpool.cpp code/buffer/buffer.cpp code/http/httprequest.cpp code/http/httpparser.cpp -pthread -o build/server_async_test_asan
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 timeout 20s ./build/server_async_test_asan LT
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 timeout 20s ./build/server_async_test_asan ET
python3 tests/lt_network.py --mode ET
python3 tests/keepalive_network.py --mode ET
python3 tests/lt_network.py --mode LT --server build/server_lt_test
python3 tests/keepalive_network.py --mode LT --server build/server_lt_test
```

结论与边界：
- 阶段 5 当前教学范围验收完成。专项的慢任务是屏障替身，不是磁盘故障实验；HUP/旧完成结果为受控注入，fd 复用由测试强制安排，不宣称全部来自自然网络时序。
- 未执行 TSan、线程创建失败/低内存/通知 fd 致命错误注入，sanitizer 结论仅覆盖本专项，不是所有 HTTP 路径。任务队列有界不等于响应结果队列、总连接或总内存都有界。
- 对用户的可读性诉求，已在阶段 8 明确加入状态枚举、异步方法命名、调用顺序和职责图整理计划；本轮不边改行为边重构。
- 下一完整单元为阶段 6 的连接无进展超时。已阅读参考 heaptimer.h/.cpp，采用到期检查思想及单调时钟，不直接继承其无符号下溢、high_resolution_clock 不保证单调、负时间转无符号等问题。首版周期扫描以减少新概念，后续再用堆优化选取到期节点。

### 阶段 6 无进展超时：基本功能通过，单次 fd 断言保留观察

实现及修复：
- 用户增加 Connection::lastActive、成功 recv/send 更新时间、ExpireIdleClients 每秒扫描和 epoll_wait(1000)。使用 steady_clock，30 秒无网络进展则关闭；不强制取消后台任务。
- 发现 run 的 EINTR 分支漏掉扫描。测试注入首次 EINTR、随后 EIO 退出，在两种模式下复现过期连接未关闭；agent 补上 ExpireIdleClients 后通过。没有进行性能重构。

实际验证：
- 扩展 server_async_test，用真实非阻塞 socketpair 加注入的过期 lastActive 验证：成功接收/部分发送续期，真实 EAGAIN 不续期，每秒扫描节流，接收/发送/后台阶段超时，后台超时后同 fd 新连接不接收旧结果；epoll_wait 的 EINTR/EIO 使用仅测试目标的链接包装。
- 新增 tests/idle_timeout_network.py，运行正式二进制，不修改 30 秒常量。每模式仅 5 个连接：空连接、半请求、Keep-Alive 空闲超时；收到数据及发送响应后续期；超时不影响活跃连接，最后新请求仍可服务。
- 首次 ET 真实网络测试在所有协议断言通过后，最终 fd 恢复检查失败。失败时未留 fd 明细，原因尚未确定；新增失败时 fd 诊断后 ET 两轮、LT 一轮通过，不能凭重跑成功宣称首次问题已解决。后续日志接入后继续观察。
- 最终 9/9 CTest 通过；LT/ET 的默认关闭网络脚本及 Keep-Alive 专项均通过。未改现有扫描策略以绕过测试断言。
- 测试服务及临时资源均回收，最终 8080 无监听。本轮没有新增 sanitizer 验收；上一版专项 ASan/UBSan 结论不能自动覆盖这次扩展。

实际命令（MyWebServer/ 下，8080 需空闲）：

```sh
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure
python3 tests/idle_timeout_network.py --mode ET
python3 tests/idle_timeout_network.py --mode LT --server build/server_lt_test
python3 tests/lt_network.py --mode ET
python3 tests/keepalive_network.py --mode ET
python3 tests/lt_network.py --mode LT --server build/server_lt_test
python3 tests/keepalive_network.py --mode LT --server build/server_lt_test
```

下一完整单元：基础同步日志，优先使关闭原因和连接身份可追踪。已审查参考 log.h/.cpp，采用时间/级别/互斥输出思想，不引入其单例宏、变参 Buffer 写入、轮转及异步组合；不照搬 localtime 共享存储和格式化长度处理风险。小根堆及异步日志等性能优化按用户要求后置。

### 阶段 6 基础同步日志：当前教学范围完成

实现与审查：
- 用户增加 code/log/logger.h/.cpp 和 Server 日志接入，保留用户的 INFO/WARN/ERROR 命名。输出时间、级别、固定事件、fd、id、错误号到 stderr，不增加日志线程或文件所有权。
- 专项复现 Log 把调用者 errno=EAGAIN(11) 改成参数 error=EPIPE(32)；修正 savedErrno 应读取 errno，而不是 error。
- 补上已声明 mutex 的 lock_guard；关闭日志的 id 使用 auto 保留 uint64_t；accept 在移动 Connection 前保存 id；发送失败删除先于 errno 保存的 perror。缺失显式锁由审查发现，不能声称并发测试复现了行交错（单次 fprintf 本身也有 stdio 同步）。

实际测试：
- tests/logger_test.cpp：日期格式/级别/错误号、超过 32 位 id、空事件、调用者 errno；4 个线程各写 100 条，检查完整行/唯一 id/总条数；Linux /dev/full 真实写入失败返回 false 且不破坏 errno。未注入 time/localtime/strftime 失败。
- tests/logging_integration.py：运行真实异步集成目标，用受控 BuildResponse 和注入过期时间检查拒绝/构造失败/超时日志；所有测试连接 id 从 2^32+1 开始，关闭日志不截断。LT/ET 正式 HTTP 进程检查接受/关闭可配对，包含合成正文/Authorization/Cookie 的请求不会将这些值输出到日志。
- tests/lt_network.py 增加可选 logs 接收列表，测试上下文结束后读取临时日志文件；不改变网络断言。日志不是写到无人消费的 PIPE，不靠日志队列或吞掉输出绕过阻塞问题。
- 10/10 CTest 通过，logger_basic 额外重复 10 次通过；LT/ET 原网络、Keep-Alive 和真实 30 秒超时各通过。此前一次 fd 回收失败本轮未复现，仍不宣称其根因已解决。
- 最终 8080 无监听，测试进程已回收；未进行本轮 ASan/UBSan/TSan，未验证服务 SIGTERM 的优雅析构或日志持久化。

新增实际命令（其余网络命令同上）：

```sh
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure
ctest --test-dir build -R '^logger_basic$' --repeat until-fail:10 --output-on-failure
python3 tests/logging_integration.py
```

范围限制：同步日志可能阻塞事件线程；fflush 不等于磁盘持久化。部分关闭分支仍用通用 closed，析构直接关闭连接不逐项记录，原有初始化错误仍有 perror；不宣称完整审计日志。日志失败返回 false，但当前 Server 不将日志失败升级为停止服务；未来可增加失效计数，不递归记录日志错误。

下一单元与环境核查：
- 继续阶段 7 单连接查询，先不接连接池/HTTP/注册登录。已重新只读审查参考 sqlconnpool.h/.cpp，不沿用连接失败覆盖原指针并把空指针入池、锁外检查队列、关闭时忽略借出连接等问题。
- pkg-config 能找到 mysqlclient，mysql 客户端存在，127.0.0.1:3306 有监听。这不是数据库认证/查询成功的证明；未读取参考凭据、未尝试账号、未建库建表或重启服务。
- 教学建议新增可选 db_smoke 目标：环境变量提供专用低权限账号，固定 SELECT 1，不访问业务表；先明确连接、结果集和库初始化的释放顺序。完整实现仍由用户编写，数据库账号及后续独立 schema 需用户确认。
- 后续登录使用预处理语句和成熟密码哈希库；本机 pkg-config 未发现 libsodium，尚未决定或安装新依赖，不用明文存储/自制散列替代。

### 阶段 7 单连接查询基本闭环：用户运行确认

- 用户编写 code/experiments/db_smoke.cpp，使用环境变量配置凭据、unique_ptr 管理 MYSQL 和 MYSQL_RES；agent 补上已声明但未设置的连接/读/写超时选项及 mysql_store_result 空指针检查。
- agent 执行 CMake 配置、db_smoke 构建成功，并在移除凭据环境变量的子进程中验证返回 1 及明确错误；未在 agent 命令中传递密码或执行数据库写入。
- 用户提供终端实测输出 PASS database SELECT 1，确认其本机学习账号的 TCP 认证、SELECT 1 结果形状和值检查通过。此前出现 1045、zsh read 参数和子 shell 环境变量作用域问题，已分别说明；不把 shell 配置错误当数据库代码故障。
- 本项不是连接池或注册登录验收；尚未验证坏 SQL、查询时断连、选项/结果集分配失败等路径。查询不指定 schema，不访问业务表。
- 下一完整单元：固定容量连接池 + 不可复制的作用域借用对象；连接不足立即失败，不增加信号量/条件变量/自动扩容/后台线程。连接池销毁前必须结束所有借用并等待相关线程，MySQL 库生命周期由外层管理，多线程使用前另行明确 mysql_thread_init/end。

### 阶段 7 连接池借还机制：测试替身验收通过

- 用户完成 code/pool/sqlconnpool.h/.cpp：池拥有全部 MYSQL，idle_ 只记录空闲指针；Take/Return 使用同一 mutex；SqlConnLease 不可复制/移动，析构归还而不是关闭。池析构要求无未结束借用，不提供并发销毁或等待关闭 API。
- 用户改造 db_smoke 为两连接顺序实验，检查两次借用不同、第三次失败、作用域归还后再次查询。已构建成功，尚未收到这个新版实验的真实数据库 PASS 输出；此前单连接 PASS 不自动覆盖连接池版本。
- agent 仅补 db_smoke 的 Threads::Threads 链接，增加 tests/sqlconnpool_test.cpp 和可选目标，无核心类修改。
- 测试通过链接包装替换 mysql_init/options/real_connect/errno/close，使用假指针作身份，不联网、不读取凭据、不执行 SQL。验证归还不关闭、最终各关闭一次、异常退栈归还，第二次 init/options/connect 失败时回滚全部资源，零容量/空账号拒绝。
- 4 个线程各 400 次少量借用尝试，验证没有同时重复借用，线程 join 后可借出完整容量；不是 MySQL 客户端库线程安全或查询正确性的证明。
- 子进程故意带未结束借用销毁池，通过自定义 terminate handler 退出码检查错误契约，不生成 core；不代表支持运行中安全关闭池。
- 11/11 CTest 通过；sqlconnpool_basic 额外重复 20 轮通过；单独编译该专项的 ASan/UBSan 通过（仍使用建连替身），未执行 TSan、内存分配失败注入或真实断连/重连验证。8080 最终无监听。

实际命令：

```sh
cmake -S . -B build -DBUILD_DB_SMOKE=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j4
ctest --test-dir build --output-on-failure
ctest --test-dir build -R '^sqlconnpool_basic$' --repeat until-fail:20 --output-on-failure
g++ -std=c++14 -g -O1 -Wall -Wextra -UNDEBUG \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  tests/sqlconnpool_test.cpp code/pool/sqlconnpool.cpp \
  $(pkg-config --cflags --libs mysqlclient) -pthread \
  -Wl,--wrap=mysql_init -Wl,--wrap=mysql_options \
  -Wl,--wrap=mysql_real_connect -Wl,--wrap=mysql_errno \
  -Wl,--wrap=mysql_close -o build/sqlconnpool_test_asan
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  timeout 20s ./build/sqlconnpool_test_asan
```

下一单元先在 db_smoke 中独立增加真实双线程查询。每个新线程在使用主线程创建的 MYSQL 前调用 mysql_thread_init，所有结果集和 lease 析构后才 mysql_thread_end；全部线程 join 后销毁池，最后 mysql_library_end。此处为当时计划，后续实现记录见下。

### 阶段 7 双线程实验：实现及错误清理专项通过，随后用户反馈真实运行通过

- 用户增加 CheckConcurrent：2 个 std::thread 各借连接执行 10 次 SELECT 1；线程创建中途失败时先 join 已创建线程再抛出；主线程等待全部 worker 后才销毁池。
- 查询失败的分支误用 return，虽能通过 lease 析构归还连接，但跳过 mysql_thread_end。agent 新增 tests/db_thread_test.cpp，在仅测试目标内包含实验源码，替换建连/线程初始化清理/查询函数，不触达真实数据库或凭据。
- 测试注入两次查询失败，修复前复现 expected thread_end=2、actual=0；将 return 最小改为 break 后通过。另验证一个线程初始化失败时不清理未成功初始化的线程，而另一个线程查询失败后清理一次，最终池容量恢复、两条连接都关闭。
- 全量构建、12/12 CTest 通过；db_thread_cleanup 额外重复 10 次通过。未注入真实线程创建失败，未新增 sanitizer/TSan 验收，没有真实数据库并发 PASS 输出，不能将模拟失败结果写成实际 MySQL 断连验证。
- 实际命令：cmake --build build -j4；ctest --test-dir build --output-on-failure；ctest --test-dir build -R '^db_thread_cleanup$' --repeat until-fail:10 --output-on-failure。最终 8080 无监听。
- 原下一教学内容为预处理语句 SELECT ?，已审查参考 UserVerify 的 SQL 拼接、密码日志、临时 RAII 和插入失败后覆盖状态问题，不照搬。
- 用户后续反馈新版 db_smoke 通过，并明确质疑实验拆分过多。复查实际 db_smoke.cpp 只有连接池/双线程查询，尚未添加 CheckPrepared；因此记录真实查询为用户确认通过，不宣称预处理语句已实现。
- 推进方式调整：不再新增独立 SELECT ? 实验，下一完整功能单元改为注册/登录从表结构、预处理语句、成熟密码哈希到工作线程及 HTTP 路由的接入。先解释完整数据流和生命周期，再分文件实现同一功能，最后集中验收；不是一次铺设无关模块。引入哈希依赖、建库建表及权限变更仍需用户确认，不能用明文密码或复用参考凭据跳过。

### 阶段 7 正式注册/密码验证后端：源码及真实 MySQL 基本闭环通过

实现与协作：
- 用户手工实现 code/auth/authservice.h/.cpp、passwordhash.h/.cpp、sql/schema.sql，并接入 main、BuildResponse 和 Server 工作任务。业务函数名采用用户的 HashPwd/VerifyPwd。
- main 按 MYWEB_AUTH=1 开启认证，环境变量提供数据库账号/密码，选择独立 mywebserver。AuthService 比 Server 活得久；Server 先销毁线程池，之后才释放数据库资源。未开启认证时旧服务仍可运行，认证接口返回 503。
- /register 与 /login 接收恰好 username/password 两个 URL-encoded 字段；用户名 1～64 字节、密码 8～128 字节，拒绝控制用户名/NUL 密码。users.username 使用 VARBINARY 主键，不以锁外 SELECT 代替数据库唯一约束。
- 密码使用本机已有 OpenSSL 的 PBKDF2-HMAC-SHA256，600000 次迭代、16 字节随机盐、32 字节结果。编码包含固定版本/参数/盐/结果，损坏格式不降级为明文。未使用此前讨论但缺少开发头文件的 libsodium，也未安装新库。
- 注册先哈希再借连接，预处理 INSERT；重复键 1062 对应 409，其他错误不报告成功。登录预处理 SELECT，复制哈希并归还连接后校验；不存在用户使用 dummyHash 完成一次计算但仍拒绝。响应 no-store，不返回密码/hash，不创建 Cookie 或会话。
- 用户完成业务源码后，agent 只改 tests/server_async_test.cpp 的四参数替身签名并新增测试文件。声明/实现的 HashPwd/VerifyPwd 名称不一致由构建发现，用户手工修正；未替用户改业务源码。后续继续保持“先讲原理与流程、给代码、用户手改、agent 验收”的边界。

agent 实际验证（不写真实数据库）：
- 全量构建及 12/12 CTest 通过；LT/ET 默认关闭网络及 Keep-Alive 四轮回归通过，保留日志 build/auth_original_network.log。
- tests/auth_business_test.cpp：真实 OpenSSL 计算与 Python hashlib 独立已知向量、随机盐、正确/错误密码、存储格式损坏；真实 AuthService/handler 的参数边界、注册/重复/不存在用户、引号输入、状态码、响应长度/no-store/无会话。
- tests/auth_mysql_stub.h/.cpp：仅测试链接包装 MySQL C API，受控内存表模拟唯一键，验证 SQL 结构固定与实际绑定字节；注入 prepare/bind/execute/store/fetch/NULL/截断、损坏 hash，检查资源回收。不是实际 MySQL 断连或 SQL 注入攻击的服务端验收。
- tests/auth_checks.py 编译测试产物到 build/，不修改 CMake/业务源码。运行正式 main/Server/线程池/epoll/eventfd/哈希代码的 ET 网络专项，验证注册登录、并发同名、Keep-Alive、分段/半关闭、错误日志与禁用认证分支；数据库仍为替身。
- python3 tests/auth_checks.py --sanitize 的业务专项 ASan/UBSan 通过；不代表真实 MySQL 库、所有网络路径或整个服务均已 sanitizer 验收。未运行 TSan。

用户提供的真实 MySQL 验收结果：
- 用户自行执行 sql/schema.sql，确认学习账号的查询/插入权限；agent 未执行 DDL、GRANT 或访问参考数据库。
- tests/auth_real_network.py 由用户交互输入数据库密码后运行，启动正式 build/server，真实 HTTP 注册返回 201、相同用户名再注册返回 409、正确密码返回 200、错误密码与不存在用户返回相同 401。
- 脚本结束首次服务并启动全新的正式服务，原用户仍可验证，确认实际 MySQL 持久化。用户粘贴了全部 PASS 输出；这是用户实测结果，不冒称由 agent 登录数据库完成。
- 本次真实脚本保留 1 个 real_check_ 前缀测试用户，不输出随机测试密码，不执行 DELETE；测试服务由脚本回收。agent 最后检查 8080 无监听。真实并发注册、真实断连/重连和权限撤销未覆盖。

实际命令（MyWebServer/ 下）：

```sh
cmake --build build -j4
ctest --test-dir build --output-on-failure
python3 tests/auth_checks.py
python3 tests/auth_checks.py --sanitize
# 以下由用户执行，会写入并保留 1 个专用测试用户：
python3 tests/auth_real_network.py
```

限制及下一步：
- 当前是账号密码验证，不是完整登录系统：无会话、权限、TLS、限流或 CSRF 防护，不用于公网真实账号。请求中的原密码仍会存在进程内存，不能宣称已安全清除所有副本。
- 工作线程内仍执行同步数据库 I/O/密码计算；有界任务队列不等于认证请求限流，数据库连接也不会自动修复。池/语句/服务销毁依赖约定的生命周期；SIGTERM 不代表优雅析构。
- 当时下一单元计划只补浏览器表单，之后集中做状态/命名/调用流的行为不变可读性整理。后续补充静态 CSS/JS/图片的实际文件服务，记录如下。

### 静态资源基本单元：接入、页面拆分及专项验收通过

实现和变更边界：
- 用户已写好 code/http/staticfile.h/.cpp 及 BuildResponse 的 /static/ 分支；agent 按用户明确授权补齐前端、CMake、测试和必要的小修，未重写核心实现，保留 MAXFILEBYTES 等命名和用户 docs/auth.md 修改。
- CMake 将 staticfile.cpp 加入 server 与 server_lifecycle_test；LT 和异步测试的复制源列表自动继承。独立 auth_checks.py 的直接编译列表也同步添加该源文件。
- 首页保留原有两个表单，将内联 CSS/JS 分别移到 resources/static/style.css、app.js；HTML 使用 link 和 defer script 加载，并引用本项目生成的 pipeline.png，无外部框架/CDN/第三方图片素材。
- 读取入口只处理受限 /static/ GET。问号之后不参与文件查找；路径组件只允许 ASCII 字母/数字/下划线/连字符/点，不接受空组件、点开头、百分号编码或反斜杠；按扩展名选择允许的 MIME，未知类型拒绝。
- 固定静态根目录使用目录 fd，逐级 openat，根/中间目录/最终文件均使用 O_NOFOLLOW；最终 O_NONBLOCK 防止误放 FIFO 在打开时阻塞，fstat 只接受普通文件。8 MiB 单文件限制先于内容分配，读取累计长度并处理 EINTR，失败不返回部分成功。FileDescriptor 析构关闭 fd。

实际验证：
- tests/staticfile_test.cpp 使用 build 工作目录下自建临时资源，验证二进制/空文件/子目录/查询参数/MIME、8 MiB 边界、真实路径/符号链接/权限/FIFO/目录拒绝、超长路径及反复成功失败的 fd 计数；退出逐项清理，不覆盖用户资源。
- 链接包装仅在 staticfile_test 中注入 read 的 EINTR、EIO、提前 EOF，以及 fstat 失败；小块读取通过限制真实 read 每次最多 3 字节覆盖累计分支。这些不是自然磁盘故障或真实并发截断测试。
- 13/13 CTest 通过（当前 BUILD_DB_SMOKE=ON），staticfile_basic 额外重复 10 次通过；读取模块专项 ASan/UBSan 通过。
- tests/static_network.py 在临时资源目录启动正式 LT/ET 二进制，原始 socket 验证首页引用的 CSS/JS/PNG 字节、Content-Type/Length、路径拒绝、POST 405/Allow、Keep-Alive/预收请求/半关闭、8 MiB 一致性、少量并发及 fd 恢复。测试资源和进程均回收。
- tests/frontend_test.js 的 Node 模拟 DOM/fetch 测试通过，保持编码、字节长度校验、状态提示、成功清空密码和失败恢复；这是前端逻辑测试，不是实际浏览器布局/渲染/无 JS 人工操作验收。
- Auth 业务/ET 网络专项通过（MySQL 替身），原 LT/ET 网络、Keep-Alive 四轮及日志集成回归通过，日志保存于 build/static_regression.log。本轮没有重新读写真实 MySQL；此前真实认证/持久化结论仍来自用户运行。
- 最终 8080 无监听。未做 TSan、mmap/writev、视频 Range、HEAD、缓存协商或吞吐量测试，不宣称自然观察到了本轮静态发送 EAGAIN。

实际命令（MyWebServer/ 下）：

```sh
cmake -S . -B build -DBUILD_DB_SMOKE=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j4
ctest --test-dir build --output-on-failure
ctest --test-dir build -R '^staticfile_basic$' --repeat until-fail:10 --output-on-failure
python3 tests/static_network.py
node tests/frontend_test.js
python3 tests/auth_checks.py
# 原 LT/ET 网络、Keep-Alive、日志脚本同此前记录。
g++ -std=c++14 -g -O1 -Wall -Wextra -UNDEBUG \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  tests/staticfile_test.cpp code/http/staticfile.cpp \
  -Wl,--wrap=read -Wl,--wrap=fstat -o build/staticfile_test_asan
(cd build && ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1 timeout 20s ./staticfile_test_asan)
```

限制：
- 静态根仍相对启动工作目录，需从项目目录启动；资源目录由开发者维护，不接受不可信上传。逐级拒绝链接不是对恶意本地文件系统操作者的完整沙箱，未处理硬链接/挂载边界或并发修改时的原子快照。
- 内存中整文件响应及 8 MiB 上限只约束单文件，不是全服务内存预算；读取仍可能阻塞工作线程，O_NONBLOCK 不保证普通文件的磁盘 I/O 异步。
- 当前以 no-store 简化开发时缓存问题。编码文件名、隐藏文件和未知类型是明确拒绝，不代表已实现通用 URL 解码/任意目录浏览。
- 下一步只整理现有处理流程与可读性，不继续叠加新业务；优化仍以后期测量为依据。
