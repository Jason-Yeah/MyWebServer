# MySQL C API 速查表

## 1. MySQL、SQL 与 C/C++ 的关系

| 名称 | 含义 | 在项目中的作用 |
| --- | --- | --- |
| MySQL Server | 数据库服务端程序 | 保存数据，接收并执行 SQL |
| SQL | 操作数据库的语言 | 例如 `SELECT` 查询、`INSERT` 插入 |
| MySQL C API | 客户端库提供的 C 接口 | 让 C/C++ 程序连接数据库、发送 SQL、读取结果 |
| `<mysql/mysql.h>` | 本机 MySQL 客户端头文件 | 提供类型和函数声明；包含头文件后，构建时还需要链接客户端库 |

C++ 可以调用这些 C 接口。下文沿用项目的 C++ 写法：空指针使用 `nullptr`；如果编写 C 代码，常见写法是 `NULL`。

## 2. 连接与查询接口

| 接口 | 作用 | 返回值判断 |
| --- | --- | --- |
| `mysql_init(nullptr)` | 创建连接对象，尚未建立连接 | 返回 `nullptr` 表示失败 |
| `mysql_options(...)` | 设置连接选项 | 返回 `0` 表示成功，非 `0` 表示失败 |
| `mysql_real_connect(...)` | 建立数据库连接并进行身份认证 | 返回 `nullptr` 表示失败 |
| `mysql_query(...)` | 执行 SQL 语句 | 返回 `0` 表示成功，非 `0` 表示失败 |
| `mysql_store_result(...)` | 获取并缓存完整结果集 | 对于产生结果集的查询（如 `SELECT`），返回 `nullptr` 表示失败；对于不产生结果集的语句，返回 `nullptr` 属于正常情况 |
| `mysql_fetch_row(...)` | 从结果集中读取一行 | 使用 `mysql_store_result()` 后，返回 `nullptr` 表示遍历结束；使用 `mysql_use_result()` 时，还需区分结束与读取错误 |
| `mysql_field_count(conn)` | 获取最近一次语句的结果列数 | `mysql_store_result()` 返回空指针时，可据此区分无结果集与获取失败 |
| `mysql_num_fields(result)` | 获取结果集的列数 | 用于确定一行中可访问的列下标范围 |
| `mysql_fetch_lengths(result)` | 获取当前行各列的字节长度 | 返回 `unsigned long*`；应在成功读取一行后调用，返回空指针表示失败 |
| `mysql_errno(conn)` | 获取最近一次相关操作的错误码 | `0` 表示无错误 |
| `mysql_error(conn)` | 获取错误说明 | 返回 `const char*`，由客户端库管理，不要自行释放 |
| `mysql_free_result(result)` | 释放结果集 | 无返回值；释放后不能继续访问该结果集及其行数据 |
| `mysql_close(conn)` | 关闭连接并释放连接资源 | 无返回值；关闭后不能继续使用该连接句柄 |

## 3. 常用类型与指针

| 类型或表达式 | 含义 | 使用要点 |
| --- | --- | --- |
| `MYSQL` | 保存连接状态的结构体类型 | 通过 API 操作，通常不直接修改其成员 |
| `MYSQL* conn` | 连接句柄 | `mysql_init(nullptr)` 创建，最终用 `mysql_close()` 释放 |
| `MYSQL_RES* result` | 结果集指针 | 由 `mysql_store_result()` 等接口返回，用 `mysql_free_result()` 释放 |
| `MYSQL_ROW row` | 一行数据；实际是 `char**` 的类型别名 | `row[i]` 表示第 `i` 列，下标从 `0` 开始；行数据由结果集管理 |
| `row[i] == nullptr` | 该列的值为 SQL `NULL` | 与空字符串 `""` 不同；读取字符串前先判断 |
| `MYSQL_FIELD` | 列的元数据 | 描述列名、类型等信息，不是某一行的列值 |
| `const char*` | 指向字符数据的指针 | 常用于主机名、用户名、SQL 等参数；指针本身不拥有字符串 |

普通查询接口返回的列值以字符数据表示。例如数据库中的整数 `42`，读取后通常是文本 `"42"`，需要检查转换结果后再作为整数使用。预处理语句接口则支持绑定数据类型。

对于包含二进制数据的列，不能只用 `strlen(row[i])` 判断长度，因为数据中可能包含 `\0`；应使用 `mysql_fetch_lengths()` 返回的对应长度。

## 4. 一次查询的生命周期

| 步骤 | 操作 | 需要注意 |
| --- | --- | --- |
| 1 | `mysql_init(nullptr)` | 检查创建是否成功；此时尚未连接服务器 |
| 2 | `mysql_options(...)` | 在连接前配置需要的选项，并检查返回值 |
| 3 | `mysql_real_connect(...)` | 连接失败时仍需清理已创建的句柄；不要丢失原指针 |
| 4 | `mysql_query(...)` | 失败后先获取错误信息，不继续按查询成功的路径读取结果 |
| 5 | `mysql_store_result(...)` | 有结果集时继续读取；无结果集的语句不进入取行循环 |
| 6 | 循环调用 `mysql_fetch_row(...)` | 逐列检查 SQL `NULL`，需要长期保存的数据应复制出来 |
| 7 | `mysql_free_result(result)` | 释放结果集，即使查询返回零行，也要释放有效的结果集对象 |
| 8 | `mysql_close(conn)` | 独立连接使用完后关闭；连接池借出的连接通常先归还池中 |

`mysql_store_result()` 返回空指针后：若 `mysql_field_count(conn) == 0`，说明语句没有结果集；若列数大于 `0`，说明本应存在结果集但获取失败，应读取错误信息。`SELECT` 查到零行通常仍返回有效的结果集对象，不能把“零行”和“空指针”混为一谈。

## 5. C++ 与连接池中的资源管理

| 问题 | 原因 | 处理方式 |
| --- | --- | --- |
| 复制 `MYSQL*` 后是否得到新连接？ | 指针复制只复制地址 | 多个指针仍指向同一个连接，需要明确所有者 |
| 能否用 `delete conn`？ | 资源由 MySQL 客户端库创建 | 使用配套的 `mysql_close()`；结果集使用 `mysql_free_result()` |
| 如何避免提前返回时泄漏？ | 手动清理容易遗漏失败分支 | 用 RAII，在析构函数中完成清理 |
| 能否直接使用默认的 `std::unique_ptr<MYSQL>`？ | 默认删除器使用 `delete` | 需要指定调用 `mysql_close()` 的自定义删除器 |
| `SqlConnLease` 析构时应该做什么？ | 借用结束后，连接还可以复用 | 通常归还连接池；连接池最终负责关闭其拥有的连接 |
| 多个线程能否同时使用同一连接？ | 同一连接具有共享的查询和结果状态 | 池应保证同一时刻一个连接只借给一个使用者 |

连接池管理的是连接的借出与归还，不会自动替调用者释放每次查询产生的结果集。归还前应结束本次查询，释放结果集，并按约定处理事务和连接状态。

## 6. 多线程与 MySQL 客户端初始化

### 6.1 区分库、线程和连接三个层次

`mysql_thread_init()` 中的“线程”指调用它的客户端线程。它为当前线程准备 MySQL 客户端所需的线程局部状态，**不会创建线程，也不会创建数据库连接或给连接加锁**。

| 层次 | 初始化与清理 | 生命周期 |
| --- | --- | --- |
| 客户端库 | `mysql_library_init()` / `mysql_library_end()` | 应用级统一管理，覆盖所有 MySQL 使用者和连接 |
| 当前线程 | `mysql_thread_init()` / `mysql_thread_end()` | 在使用 MySQL 的线程中初始化，在同一线程结束使用后清理 |
| 数据库连接 | `mysql_init()`、`mysql_real_connect()` / `mysql_close()` | 连接池可以持有连接，供不同线程先后独占借用 |

**主线程创建了连接，不代表工作线程已经完成线程初始化。** 例如主线程构造 `SqlConnPool`，工作线程只借出已有的 `MYSQL*` 来查询：借用操作没有调用 `mysql_init()`，因此工作线程需要单独考虑 `mysql_thread_init()`。[官方多线程说明](https://dev.mysql.com/doc/c-api/8.4/en/c-api-threaded-clients.html)

### 6.2 常用并发接口

| 接口 | 作用 | 返回值与使用时机 |
| --- | --- | --- |
| `mysql_library_init(0, nullptr, nullptr)` | 初始化客户端库 | `0` 成功，非 `0` 失败；建议在主线程创建工作线程前完成 |
| `mysql_library_end()` | 清理客户端库资源 | 无返回值；所有使用者结束且连接关闭后调用 |
| `mysql_thread_init()` | 初始化当前线程的 MySQL 状态 | `0` 成功，非 `0` 失败；失败后不应继续执行该线程中的数据库操作 |
| `mysql_thread_end()` | 清理当前线程的 MySQL 状态 | 无返回值；在该线程最后一次 MySQL 操作之后调用，不会关闭连接 |
| `mysql_thread_safe()` | 查询客户端库是否按线程安全方式构建 | 非 `0` 表示是；不代表同一个连接可以被多个线程同时操作 |

`mysql_library_init()` 的全局初始化过程不是线程安全的。虽然 `mysql_init()` 可以按需隐式初始化库，但多线程程序应提前统一初始化，避免多个线程竞争首次初始化。[库初始化说明](https://dev.mysql.com/doc/c-api/8.4/en/mysql-library-init.html)

当前线程调用 `mysql_init()` 时，会自动调用线程初始化；`mysql_library_init()` 也会为调用它的线程完成这一步。所以不是所有路径都需要重复显式调用 `mysql_thread_init()`，关键是每个使用 MySQL 的线程都已初始化。[线程初始化说明](https://dev.mysql.com/doc/c-api/8.4/en/mysql-thread-init.html)

关于清理的版本细节：MySQL 8.4 官方说明，无调试支持的发布构建可以不调用 `mysql_thread_end()`；调试构建需要配对清理，否则可能泄漏线程相关资源，而且客户端库不会自动调用它。学习项目可以统一采用“线程初始化成功后，在线程结束使用时清理”的结构，避免依赖构建差异。[线程清理说明](https://dev.mysql.com/doc/c-api/8.4/en/mysql-thread-end.html)

### 6.3 与线程池、连接池如何配合

| 阶段 | 所在线程 | 操作 |
| --- | --- | --- |
| 程序启动 | 主线程 | 检查 `mysql_library_init()` 是否成功，再创建连接池和工作线程 |
| 工作线程启动 | 每个需要使用 MySQL 的工作线程 | 初始化当前线程的 MySQL 状态；只借用已有连接时，显式调用 `mysql_thread_init()` |
| 执行数据库任务 | 工作线程 | 创建 `SqlConnLease`，检查借用成功后执行查询 |
| 任务结束 | 工作线程 | 释放结果集、处理事务和连接状态，再由 lease 归还连接 |
| 工作线程退出 | 该工作线程 | 在所有数据库任务和借用对象结束后调用 `mysql_thread_end()` |
| 程序关闭 | 主线程 | 停止提交任务，等待工作线程结束，再销毁连接池 |
| 最终清理 | 主线程 | 完成主线程所需的线程清理，最后调用 `mysql_library_end()` |

对于长驻工作线程，线程初始化与清理适合包围整个任务循环，而不是每执行一次 SQL 就调用一对。若用 RAII 守卫封装线程状态，守卫应在线程入口创建，并比该线程的所有数据库任务、结果集和借用对象活得更久；它只负责线程状态，连接仍由连接池管理。

当前项目的 `ThreadPool::WorkerLoop()` 是任务循环入口，可据此理解上述生命周期；这一节只说明接入位置，并未修改线程池实现。若在线程入口初始化失败，应设计失败通知和退出路径，不能让异常直接逃出 `std::thread` 的入口函数，否则会触发 `std::terminate()`。

### 6.4 初始化不能代替并发控制

| 常见误解 | 正确理解 |
| --- | --- |
| 每个线程都调用了 `mysql_thread_init()`，就可以共享同一连接同时查询 | 线程状态独立不等于连接状态独立；本项目采用一个连接同一时刻只借给一个使用者 |
| 主线程初始化过，工作线程就不用管了 | 线程局部状态属于各自线程，不能由主线程替其他线程初始化 |
| `mysql_thread_end()` 会归还或关闭连接 | 它只清理当前线程的客户端状态；归还由 lease 完成，关闭由连接所有者完成 |
| 连接池借还操作加了锁，SQL 执行也自动受到保护 | 借还锁保护容器；查询期间依靠独占借用，禁止复制裸指针后交给其他线程同时使用 |
| 连接不重复借出，所有数据库并发问题就解决了 | 不同连接仍可能同时修改同一份数据库数据，需要另行理解事务、隔离级别和数据库锁 |

单线程的借用、耗尽、归还实验只能验证基本借还行为。验证并发还需要实际启动多个线程，检查独占借用、归还同步以及退出时的资源顺序。

## 参考资料

- [MySQL C API 官方指南](https://dev.mysql.com/doc/c-api/8.4/en/)
- [基本数据结构](https://dev.mysql.com/doc/c-api/8.4/en/c-api-data-structures.html)
- [基本调用流程与结果集管理](https://dev.mysql.com/doc/c-api/8.4/en/c-api-basic-interface-usage.html)
- [mysql_store_result() 的返回值](https://dev.mysql.com/doc/c-api/8.4/en/mysql-store-result.html)
- [mysql_fetch_row() 的返回值](https://dev.mysql.com/doc/c-api/8.4/en/mysql-fetch-row.html)
- [mysql_close() 的资源释放约定](https://dev.mysql.com/doc/c-api/8.4/en/mysql-close.html)
- [编写多线程客户端](https://dev.mysql.com/doc/c-api/8.4/en/c-api-threaded-clients.html)
- [mysql_library_init()](https://dev.mysql.com/doc/c-api/8.4/en/mysql-library-init.html)
- [mysql_library_end()](https://dev.mysql.com/doc/c-api/8.4/en/mysql-library-end.html)
- [mysql_thread_init()](https://dev.mysql.com/doc/c-api/8.4/en/mysql-thread-init.html)
- [mysql_thread_end()](https://dev.mysql.com/doc/c-api/8.4/en/mysql-thread-end.html)
