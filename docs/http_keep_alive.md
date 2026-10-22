# HTTP KEEP-ALIVE

After a request and its response are completed, keep the TCP connection open so that subsequent requests can continue to use it.

## Not reuse connections

For instance:

```text
Establish connection -> Request homepage -> Return homepage -> Close;
Establish connection -> Request CSS -> Return CSS -> Close;
...
```

## Use Keep-Alive

```text
Establish connection:
    -> Request homepage -> Return homepage
    -> Request CSS -> Return CSS
    -> Request image -> Return image
    -> ...
Close connection;
```

So, this can reduce the overhead of repeatedly establishing TCP connections.

`HTTP/1.1` defaultly uses `keep-alive`, and sends this header means closing connection after response ending.

```http
Connection: close
```
