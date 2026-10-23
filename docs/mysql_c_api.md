# MySQL C API 速查表

| 接口 | 作用 | 返回值判断 |
| --- | --- | --- |
| `mysql_init(nullptr)` | 创建连接对象，尚未建立连接 | 返回 `nullptr` 表示失败 |
| `mysql_options(...)` | 设置连接选项 | 返回 `0` 表示成功，非 `0` 表示失败 |
| `mysql_real_connect(...)` | 建立数据库连接并进行身份认证 | 返回 `nullptr` 表示失败 |
| `mysql_query(...)` | 执行 SQL 语句 | 返回 `0` 表示成功，非 `0` 表示失败 |
| `mysql_store_result(...)` | 获取并缓存完整结果集 | 对于产生结果集的查询（如 `SELECT`），返回 `nullptr` 表示失败；对于不产生结果集的语句，返回 `nullptr` 属于正常情况 |
| `mysql_fetch_row(...)` | 从结果集中读取一行 | 返回 `nullptr` 表示没有可读取的行 |
