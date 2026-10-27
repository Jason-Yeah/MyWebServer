# MyWebServer

 Linux C++ HTTP 服务器学习项目，用于理解网络 I/O、HTTP 解析、并发处理和资源生命周期，非生产级服务器。

## 功能与技术

- **非阻塞 Socket + epoll**：支持 LT/ET，正式入口使用 ET；事件线程负责网络 I/O，线程池负责业务和响应构造。
- **增量 HTTP 解析 + 动态 Buffer**：支持受限 HTTP/1.1 GET/POST、分段请求、正文与表单解析。
- **静态资源**：提供 HTML、CSS、JS 和常见图片，包含路径检查、禁止跟随符号链接、MIME 识别及 8 MiB 单文件限制。
- **连接管理**：显式 Keep-Alive、每连接最多 16 次请求、30 秒网络无进展超时、部分读写与断连处理。
- **线程间协作**：有界任务队列、完成队列与 eventfd 通知，使用 fd/id 区分复用前后的连接。
- **注册与密码验证**：MySQL C API、预处理语句、RAII 连接池，以及 OpenSSL PBKDF2-HMAC-SHA256 加盐密码存储。
- **基础日志与测试**：互斥保护的同步日志、CTest、LT/ET 网络回归、故障注入及部分 ASan/UBSan 专项。

## 环境要求

| 项目 | 要求 |
| --- | --- |
| 系统 | Linux，支持 WSL2；已验证 Ubuntu 24.04 / WSL2，**未验证最低发行版或内核版本** |
| C++ | **C++14 或以上**；当前验证 G++ 13 |
| 构建 | **CMake ≥ 3.16**、构建工具、pkg-config、系统线程库 |
| 数据库 | MySQL 8 客户端开发库；开启注册验证时另需可访问的 MySQL 服务 |
| 密码库 | OpenSSL Crypto 开发库，已验证 OpenSSL 3.0 |
| 测试工具 | Python 3；前端模拟测试另需 Node.js，不影响正常服务运行 |

Ubuntu 可先安装构建依赖：

```sh
sudo apt install build-essential cmake pkg-config libmysqlclient-dev libssl-dev
```

项目依赖 Linux 的 epoll、eventfd、openat 等接口，不直接支持 Windows 原生环境；接口存在不代表已验证某个旧内核与全部依赖兼容。

## 快速启动

### 1. 构建（在 MyWebServer 目录中）

```sh
cmake -S . -B build -DBUILD_DB_SMOKE=ON
cmake --build build -j4
```

`BUILD_DB_SMOKE` 仅增加学习实验及相关测试，正式服务器不依赖先运行这些实验。

### 2. 先体验页面和静态资源

```sh
MYWEB_AUTH=0 ./build/server
```

访问 http://127.0.0.1:8080/。默认仅监听本机；未开启认证时，合法注册/验证请求返回 503。直接运行服务器需保持项目工作目录。

### 3. 开启注册与密码验证（首次配置一次）

使用管理员账号执行建表脚本，例如本机支持 sudo/socket 登录时：

```sh
sudo mysql < sql/schema.sql
cp -n .env.example .env.local  # 不覆盖已有的本机配置
chmod 600 .env.local
```

编辑 `.env.local`：保持 `MYWEB_AUTH=1`，填写自己的 MySQL 账号与密码。该账号需要 `mywebserver.users` 表的 **SELECT、INSERT** 权限，授权由管理员完成；建表脚本不会创建账号或自动授权。

以后使用启动脚本即可：

```sh
bash ./run.sh
```

脚本切换到项目目录，加载配置，再启动服务器。配置采用 Bash 语法，特殊字符需正确引用；**只加载自己创建、可信的配置文件**。

> 网站测试账号与 MySQL 连接账号不同。当前只实现注册和密码验证，成功不创建登录会话；无 HTTPS。
>
## TODO

- [ ] 小根堆定时器替换周期扫描，补充专项测试。
- [ ] 有界异步日志队列及关闭排空机制。
- [ ] `--port`、`--root` 启动参数与轻量健康检查。
- [ ] 连接数量、全局响应内存预算及优雅退出。
- [ ] 性能测量后再评估 mmap/writev 等优化。
