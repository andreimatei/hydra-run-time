#! /usr/bin/env python

import os, sys, thread, time, random, socket

fifo_in  = "/tmp/fifo-to-daemon"
fifo_out = "/tmp/fifo-from-daemon"

pid = 0
command_line = ""  # name of the binary to launch

def launch_process():
    global pid
#time.sleep(2)
#pid = os.spawnlp(os.P_NOWAIT, 'echo', 'test', '/tmp/fifo-to-daemon')
    ss = str(random.randrange(1,100))
    print 'launching: %s with %s,%s' % (command_line, fifo_in, fifo_out)
    pid = os.spawnlp(os.P_NOWAIT, command_line, 'test', '/tmp/fifo-to-daemon', fifo_in, fifo_out)
    print 'PID = %d' % pid

def parse_memory_map(pid):
    lst = []
    print 'opening /proc/%d/maps' % pid
    f = open('/proc/%d/maps' % pid, 'r')
    for line in f:
        index = line.find('-');
        l,sep,rest = line.partition('-')
        r,sep,rest = rest.partition(' ')
        print 'found %s -> %s' % (l, r)
        lst.append((l, r))
    return lst

def wait_for_primary_node():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    host = ''
    port = 27272
    backlog = 5
    #size = 1024
    sock.bind((host, port))
    sock.listen(backlog)
    client, address = sock.accept()
    #data = client.recv(size)

    return client, address

def send_map_to_primary_node(client, address, lst):
    s = ''
    s += str(len(lst))
    #s += ';'
    #first = True
    for (l,r) in lst:
        s += ";%s-%s" % (l,r)
    s += '!'
    client.sendall(s)


def main(argv=None):
    global pid, command_line, fifo_in, fifo_out
    if argv is None:
        argv = sys.argv

    r = random.randrange(1, 10000)
    fifo_in = fifo_in + '_' + str(r)
    fifo_out = fifo_out + '_' + str(r)


    if os.path.exists(fifo_in):
        os.remove(fifo_in)
    if os.path.exists(fifo_out):
        os.remove(fifo_out)
    
    os.mkfifo(fifo_in)
    os.mkfifo(fifo_out)
    
    command_line = argv[1]

    print 'launching %s' % command_line
    launch_process()
    print 'Opening fifo'
#thread.start_new_thread(launch_process, ())

    pipein = open(fifo_in, 'r');

    #wait until process is started
    print 'Reading from fifo'
    line = pipein.readline()

    print "Child process is loaded... it said %s" % line
    print 'PID = %d' % pid

    #parse memory map of child process
    lst = parse_memory_map(pid)
    print 'Waiting for primary...'
    client, address = wait_for_primary_node()
    print 'Got client. Sending memory map...'
    send_map_to_primary_node(client, address, lst)
    print 'Done sending memory map'

    os.remove(fifo_in)
    os.remove(fifo_out)
    return 0


if __name__ == "__main__":
    sys.exit(main())


