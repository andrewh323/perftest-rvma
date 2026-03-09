# Currently Fixing
rdma_resolve_addr: Invalid argument
iperf3: error - unable to connect stream: Invalid argument
On client side...

# Need to Fix
The whole code, maybe rewrite in UCP/UCT but I need to architecture maxx it.
How can I make it benchmark agnostic, what needs to be different and what calls need to be ignored based on benchmark, is there a way of notifying?

# Research
- I believe that the issue is arising from different VADDRs being created, but where is this happening I don't know yet.
- I have found that on rvconnect in client, ip_host_order resolves to 0, which is wrong in comparison to the socket_vaddr generated in server_shim. This is the next order of business.
- It's been 15 minutes, I got the same vaddr and its still dying.

- The rdma_resolve_addr/rdma_bind_addr is causing me big issues, checking null rn

# Epiphany (2026-03-06)
- It is for sure a scope issue and I/andrew are storing everything on the stack

# Epiphany 2 (2026-03-08)
- Seems I cannot open a iperf3 on PORT 1023, WHY? Who knows? but that is where the rdma cm_id is :D

# Debugging Tools
- Ethtool
- iplink/ifconfig
- tcpdump
-netstat/ss
- lldptool/dcbtool
