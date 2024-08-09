#!/bin/bash
qemu-system-x86_64              \
  -m 2048M                                                \
  -smp 2                                                  \
  -boot c 	                                          \
  -nographic                                              \
  -serial mon:stdio\
  -nic user,hostfwd=tcp::10022-:22,mac=00:11:22:33:44:55  \
  -chardev socket,id=ttySPI,server=on,wait=off,host=0.0.0.0,port=23456\
  -device isa-serial,chardev=ttySPI,id=serialSilKit,index=1\
  -netdev socket,id=mynet0,listen=:12345                                \
  -net nic,macaddr=52:54:56:53:4B:51,netdev=mynet0                      \
  -hda silkit-qemu-demos-guest.qcow2 \
  -qmp tcp:localhost:4444,server,wait=off
