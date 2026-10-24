#pragma once
#include <string>

// 只用于测试。真实 MySQL 服务由内存替身代替，绝不操作本机数据库。
enum class AuthDbFailure { None, Prepare, Bind, Execute, Store, Fetch, NullResult, Truncate };
void AuthDbFailNext(AuthDbFailure failure);
void AuthDbReset();
std::string AuthDbStoredHash(const std::string& username);
void AuthDbCorruptHash(const std::string& username);
int AuthDbLiveConnections();
int AuthDbLiveStatements();
int AuthDbThreadBalance();
