Static TCP
==========

Pluggable Linux congestion control module for Static TCP. Disables TCP
congestion control and defines a static value for the congestion window cwnd.

Useful commands
---------------

`sudo insmod ./tcp-static.ko` load the compiled kernel module

`sudo rmmod ./tcp-statuc.ko` unload the kernel module

`sysctl net.ipv4.tcp_congestion_control` show the currently used algorithm

`sudo sysctl -w net.ipv4.tcp_congestion_control=static` change the currently used cc algorithm

`echo 1 > /sys/module/tcp_static/parameters/static_cwnd` set the congestion window
