# Pipeline

Event Thread:

Receive request -> Parse request -> Delegate the task of constructing the response

↓

Worker Thread: BuildResponse()

↓

Event Thread: Send the constructed response

## Design Reason

Worker thread do not touch tasks about **Connection**.

If worker thread directly use such as `Connection& conn`.

Meanwhile, the event thread detects that the client has disconnected and executes: `connections_.erase(fd);`

So the `conn` of worker thread becomes a invaild reference.





2026-10-23
