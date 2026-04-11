# 7. Client State Machine

## States

```
Error States (negative):
  -6  connect_token_expired
  -5  invalid_connect_token
  -4  connection_timed_out
  -3  connection_response_timed_out
  -2  connection_request_timed_out
  -1  connection_denied

Normal States:
   0  disconnected (initial)
   1  sending_connection_request
   2  sending_connection_response
   3  connected (goal)
```

## Transitions

```
disconnected → sending_connection_request  (client calls connect with token)
sending_connection_request → sending_connection_response  (received challenge)
sending_connection_request → connection_denied  (received denied)
sending_connection_request → connection_request_timed_out  (timeout)
sending_connection_response → connected  (received keep-alive)
sending_connection_response → connection_denied  (received denied)
sending_connection_response → connection_response_timed_out  (timeout)
connected → connection_timed_out  (no packets within timeout)
connected → disconnected  (received disconnect or client disconnects)
any → connect_token_expired  (token expired during connection attempt)
```

## Multi-Server Fallback

If connection to server N fails (denied or timeout), the client automatically tries server N+1 from the connect token's server list. Only when ALL servers exhausted does the client enter an error state.

## Relevant for netudp

netudp should adopt this state machine with the same detailed error states. The multi-server fallback is particularly valuable for matchmaking.
