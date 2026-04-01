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

# Epiphany 3 (2026-03-21)
- After capstone hell I have made some of the most shoddy code I have ever written. I am fitting a square into an amorphous blob hole, what is libc and sockets god help me. New error though so we might be cooking? 377 most useful class on god, and 373.

# Epiphany 4 (2026-03-23)
- RDMA_CM_EVENT_REJECTED is the error that is the bane of my existence, it does bind for first socket but not for the next? Why does iperf give me a new address even though I told it to use ib0? Thank you Narval, very cool.
- TCP Control Socket is created with ephemeral port if that matters.
- By forcing AF_INET it seems to remove an error and fix the rdma_resolve_addr error?

# Epiphany 5 (2026-03-24)
- Ok so I accidentally ran the automake thing and it reset my Makefile, I have the rules saved but then I tried getting it to work with Makefile.am, now the shared libaries hang... GREAT. and I cant even link in log.c properly...

# Epiphany 6 (2026-03-24 @ 10:27 PM)
Ok for some reason it is making a DGRAM socket? Also I see in connect it gets really mad at me and seems to be still going to generate a real socket when I pass the rvma fd, even though obviously its not a real socket.

# Epiphany 7 (2026-04-01 @ 12:19 PM)
After finishing my capstone report I realize that I suffer from brain damage and that this makes sense again for the next 5 minutes.

# Epiphany 8 (2026-04-01 @ 1:45 PM)
I have taken a step back to make a dumb pipe, i.e. basic client/server and get that to work first.
For some reason it is creating 2 sockets now and dying over it.

# Epiphany 9 (2026-04-01 @ 1:55 PM)
I AM CREATING THE SOCKET USING THE NODE'S IB IP, RAHHH.

# Epiphany 10 (2026-04-01 @ 2:05 PM)
The buffer is not being received and I do not know where its being double called in terms of socket creation. rdma_resolve_addr is failing again.

# Epiphany 11
Now need to fix sending/recivng not working properly, I made a minor hotfix for the socket issue. Also remember to change the IPs in tcp_server, tcp_client, and rvma_pipe to ib0.

# Epiphany 12 (2026-04-01 @ 5:20 PM)
I give up for now. Send/recv for some reason no work even on the basic shim. I have to get this to work first for 497.

# Debugging Tools
- Ethtool
- iplink/ifconfig
- tcpdump
-netstat/ss
- lldptool/dcbtool

# Benchmarks
https://computing.llnl.gov/projects/sphinx-integrated-parallel-microbenchmark-suite/files-distribution
https://iperf.fr
