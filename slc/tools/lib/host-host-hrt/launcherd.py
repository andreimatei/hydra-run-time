#! /usr/bin/env python

import os, sys, thread, time, random

fifo_in  = "/tmp/fifo-to-daemon"
fifo_out = "/tmp/fifo-from-daemon"

pid = 0

def launch_process():
  global pid
#time.sleep(2)
#pid = os.spawnlp(os.P_NOWAIT, 'echo', 'test', '/tmp/fifo-to-daemon')
  ss = str(random.randrange(1,100))
  print 'launching: %s with %s' % ('test', ss)
  pid = os.spawnlp(os.P_NOWAIT, './test', 'test', '/tmp/fifo-to-daemon', ss)
#pid = os.system("./test");
# pid = os.system("test");
  print 'PID = %d' % pid
#pid = os.spawnlp(os.P_NOWAIT, name, name, arguments)


def parse_memory_map(pid):
  print 'opening /proc/%d/maps' % pid
  f = open('/proc/%d/maps' % pid, 'r')
  for line in f:
    index = line.find('-');
    l,sep,rest = line.partition('-')
    r,sep,rest = rest.partition(' ')
    print 'found %s -> %s' % (l, r)


def main(argv=None):
  if argv is None:
    argv = sys.argv
  if not os.path.exists(fifo_in):
    os.mkfifo(fifo_in)
  if not os.path.exists(fifo_out):
    os.mkfifo(fifo_out)

#pipein = open(fifo_in, os.O_RDONLY | os.O_NONBLOCK)

#pid = os.spawnlp(os.P_NOWAIT, 'test', 'test', '/tmp/fifo-to-daemon')
  #pid = os.spawnlp(os.P_NOWAIT, argv[1], argv[1], fifo_in, fifo_out)
# print 'PID = %d' % pid
  launch_process()
  print 'Opening fifo'
#thread.start_new_thread(launch_process, ())

  pipein = open(fifo_in, 'r');

  #wait until process is started
#pipein = open(fifo_in, 'r')
  print 'Reading from fifo'
  line = pipein.readline()
  
  print "Child process is loaded... it said %s" % line
  print 'PID = %d' % pid

  #parse memory map of child process
  parse_memory_map(pid)

  os.remove(fifo_in)
  os.remove(fifo_out)
  return 0


if __name__ == "__main__":
  sys.exit(main())

  
