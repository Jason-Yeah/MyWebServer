# Auth Part

## Regiser

Input: "P" password;

1. Generat random `salt` S
2. Calculate **K = PBKDF2-HMAC-SHA256(P,S, ITERATIONS)**;
3. Save: encoded = S, K, algorithm + parameters; <=> INSERT INTO DATABASE

## Login

Input: "P2" password;

1. Read S, K from DATABASE <=> SELECT xxx FROM xxx
2. re_calcu **K2 = PBKDF2-HMAC-SHA256(P2, S, ITERATIONS)**;
3. Cmp K2, K;

## HashPwd

1. Prepare `array<unsigned char, SALT_BYTES> salt{}`
2. Random padding: `RAND_bytes(salt.data(), salt.size())`
3. Calculate password: `function::Derived(pwd, salt)`
   1. `PKCS5_PBKDF2_HMAC(pwd,pwdsize,salt,saltsize,ITERATIONS, EVP_sha256(),keysize,key);` -> Get **key**
   2. key = PBKDF2(pwd, salt, ITERATIONS)
4. encoded = PREFIX + Hex(salt) + $ + Hex(key)
   1. Hex(digith to hex)
5. Finally get: `pbkdf2-sha256$600000$<salt>$<key>`

## VerifyPwd

This function does not call `HashPwd` that will generater a new `salt` value.

1. `encoded.compare(0, saltStart, PREFIX)`
2. encoded -> Unhex(...); -> salt K
3. Calculate `Derived(password_2, salt)` -> K2
4. CRYPTO_memcmp(K2, K)

## SQL PREPARE

1. prepare -> confirm the struct of SQL
2. bind -> param data
3. execute -> execute SQL_STMT

## Auth In DATABASE

Table: 
- username: `jason`
- password_hash: `pbkdf2-sha256$60000$....`  // Not origin password

## Servic

AuthService::Register() -> AuthResult::Registered

BuildResponse() -> tranform `Registered` to `HTTP 201`

Server -> Send Response
