

 PHP多版本下安装Swoole引起的问题

    php swoole 

1k 次阅读  ·  读完需要 4 分钟
问题

首先,你电脑上，系统是Ubuntu是安装了很多版本的PHP，其次，你的PHP引用改了之后有多个引起多个版本扩展共存的问题
即如在我本地为/etc/php/7.1/cli目录下
图片描述

然后在/usr/lib/php目录下会是这样：
图片描述

这种情况下使用pecl进行安装将会出现

Module compiled with module API=20151012 
PHP    compiled with module API=20160303

的情况，这样你使用php -v会一起报这个问题，如果不是这个问题就不用往下看了。
解决

首先，查看 /usr/bin/php-config这个软连接指向的是那个，如果发现本地只有一个即如php-config7.0可断定安装的扩展与实际运行的PHP版本不对应，需要安装dev
你要重新根据自己PHP版本安装dev扩展,我正在使用的是PHP7.1.25
在终端输入：

sudo apt-get install php7.1-dev

安装好后，进入目录/usr/bin下,查看

clipboard.png

然后备份旧版本的软连接，创建所需版本的软连接，终端：

 sudo mv /usr/bin/phpize /usr/bin/phpize-old
 sudo ln -s /usr/bin/phpize7.1 /usr/bin/phpize
 
 sudo mv /usr/bin/php-config /usr/bin/php-config-old
 sudo ln -s /usr/bin/php-config7.1 /usr/bin/php-config

最后：
如果之前安装过先将原来PHP.ini的extension=swoole.so先删了，
再终端 sudo pecl uninstall swoole
再运行 sudo pecl install swoole

查看php.ini当前版本位置：php --ini
安装后：sudo gedit /path/to/php/7.1/cli/php.ini 因为可能有些人不是和我安装一样默认目录，所以自行查看，将extension=swoole.so加入到文件中

通过 php -m | grep swoole
如果正常会显示：

clipboard.png

make install后会在/home/wangchao/php74/lib/php/extensions/no-debug-non-zts-20190529/下出现swoole.so文件

最后在'/home/wangchao/php74/lib/php.ini' 里加入一句extension=swoole.so





Swoole
======
[![Build Status](https://api.travis-ci.org/swoole/swoole-src.svg)](https://travis-ci.org/swoole/swoole-src)

Swoole is an event-based & concurrent framework for internet applications, written in C, for PHP.

__IRC__:  <http://webchat.freenode.net/?channels=swoole&uio=d4>

event-based
------

The network layer in Swoole is event-based and takes full advantage of the underlaying epoll/kqueue implementation, making it really easy to serve thousands of connections.

concurrent
------

On the request processing part, Swoole uses a multi-process model. Every process works as a worker. All business logic is executed in workers, synchronously.

With the synchronous logic execution, you can easily write large and robust applications and take advantage of almost all libraries available to the PHP community.

in-memory
------

Unlike traditional apache/php-fpm stuff, the memory allocated in Swoole will not be free'd after a request, which can improve preformance a lot.


## Why Swoole?

Traditional PHP applications almost always run behind Apache/Nginx, without much control of the request. This brings several limitations:

1. All memory will be freed after request. All PHP code needs be re-compiled on every request. Even with opcache enabled, all opcode still needs to be re-executed.
2. It is almost impossible to implement long connections and connections pooling techniques.
3. Implementing asynchronous tasks requires 3rd party queue servers, such as rabbitmq and beanstalkd.
4. Implementing realtime applications such as chatting server requires 3rd party languages, nodejs for example.

This why Swoole appeared. Swoole extends the use cases of PHP, and brings all these possibilities to the PHP world. 
By using Swoole, you can build enhanced web applications with more control, real-time chatting servers, etc more easily.

## Requirements

* PHP 5.3.10 or later
* Linux, OS X and basic Windows support (Thanks to cygwin)
* GCC 4.4 or later

## Installation

1. Install via pecl
    
    ```
    pecl install swoole
    ```

2. Install from source

    ```
    sudo apt-get install php5-dev
    git clone https://github.com/swoole/swoole-src.git
    cd swoole-src
    phpize
    ./configure
    make && make install
    ```

## Introduction

Swoole includes components for different purposes: Server, Task Worker, Timer, Event and Async IO. With these components,
Swoole allows you to build many features.


### Server

This is the most important part in Swoole. It provides necessary infrastructure to build server applications. 
With Swoole server, you can build web servers, chat messaging servers, game servers and almost anything you want.

The following example shows a simple echo server.

~~~php
// create a server instance
$serv = new swoole_server("127.0.0.1", 9501); 

// attach handler for connect event, once client connected to server the registered handler will be executed
$serv->on('connect', function ($serv, $fd){  
    echo "Client:Connect.\n";
});

// attach handler for receive event, every piece of data received by server, the registered handler will be
// executed. And all custom protocol implementation should be located there.
$serv->on('receive', function ($serv, $fd, $from_id, $data) {
    $serv->send($fd, $data);
});

$serv->on('close', function ($serv, $fd) {
    echo "Client: Close.\n";
});

// start our server, listen on port and ready to accept connections
$serv->start();
~~~

Try to extend your server and implement what you want!

### Http Server

```php
$http = new swoole_http_server("0.0.0.0", 9501);

$http->on('request', function ($request, $response) {
    $response->header("Content-Type", "text/html; charset=utf-8");
    $response->end("<h1>Hello Swoole. #".rand(1000, 9999)."</h1>");
});

$http->start();
```

### WebSocket Server

```php
$ws = new swoole_websocket_server("0.0.0.0", 9502);

$ws->on('open', function ($ws, $request) {
    var_dump($request->fd, $request->get, $request->server);
    $ws->push($request->fd, "hello, welcome\n");
});

$ws->on('message', function ($ws, $frame) {
    echo "Message: {$frame->data}\n";
    $ws->push($frame->fd, "server: {$frame->data}");
});

$ws->on('close', function ($ws, $fd) {
    echo "client-{$fd} is closed\n";
});

$ws->start();
```

### Task Worker

Swoole brings you two types of workers: server workers and task workers. Server workers are for request
handling, as demonstrated above. Task workers are for task execution. With task workers, we can make our 
task executed asynchronously without blocking the server workers.

Task workers are mainly used for time-consuming tasks, such as sending password recovery emails. And ensure
the main request returns as soon as possible.

The following example shows a simple server with task support.

```php
$serv = new swoole_server("127.0.0.1", 9502);

// sets server configuration, we set task_worker_num config greater than 0 to enable task workers support
$serv->set(array('task_worker_num' => 4));

// attach handler for receive event, which have explained above.
$serv->on('receive', function($serv, $fd, $from_id, $data) {
    // we dispath a task to task workers by invoke the task() method of $serv
    // this method returns a task id as the identity of ths task
    $task_id = $serv->task($data);
    echo "Dispath AsyncTask: id=$task_id\n";
});

// attach handler for task event, the handler will be executed in task workers.
$serv->on('task', function ($serv, $task_id, $from_id, $data) {
    // handle the task, do what you want with $data
    echo "New AsyncTask[id=$task_id]".PHP_EOL;

    // after the task task is handled, we return the results to caller worker.
    $serv->finish("$data -> OK");
});

// attach handler for finish event, the handler will be executed in server workers, the same worker dispatched this task before.
$serv->on('finish', function ($serv, $task_id, $data) {
    echo "AsyncTask[$task_id] Finish: $data".PHP_EOL;
});

$serv->start();
```

Swoole also supports synchronous tasks. To use synchronous tasks, just simply replace 
`$serv->task($data)` with `$serv->taskwait($data)`. Unlike `task()`, `taskwait()` will wait for a task to
complete before it returns its response.

### Timer

Swoole has built in millisecond timer support. By using the timer, it is easy to get a block of code
executed periodically (really useful for managing interval tasks).

To demonstrate how the timer works, here is a small example:

```php
//interval 2000ms
$serv->tick(2000, function ($timer_id) {
    echo "tick-2000ms\n";
});

//after 3000ms
$serv->after(3000, function () {
    echo "after 3000ms.\n"
});
```

In the example above, we first set the `timer` event handler to `swoole_server` to enable timer support.
Then, we add two timers by calling `bool swoole_server::addtimer($interval)` once the server started. 
To handle multiple timers, we switch the `$interval` in registered handler and do what we want to do.

### Event

Swoole's I/O layer is event-based, which is very convenient to add your own file descriptor to Swoole's main eventloop.
With event support, you can also build fully asynchronous applications with Swoole.

To use events in Swoole, we can use `swoole_event_set()` to register event handler to sepecified file descriptor, 
once registered descriptors become readable or writeable, our registered handler will be invoked. Also, we can 
using `bool swoole_event_del(int $fd);` to remove registered file descriptor from eventloop.

The following are prototypes for the related functions:

```php
bool swoole_event_add($fd, mixed $read_callback, mixed $write_callback, int $flag);
bool swoole_event_set($fd, mixed $read_callback, mixed $write_callback, int $flag);
bool swoole_event_del($fd);
```

The `$fid` parameter can be one of the following types:

* unix file descriptor
* stream resource created by `stream_socket_client()/fsockopen()`
* sockets resources created by `socket_create()` in sockets extension (require compile swoole with --enable-sockets support)

The `$read_callback` and `$write_callback` are callbacks for corresponding read/write event.

The `$flag` is a mask to indicate what type of events we should get notified, can be `SWOOLE_EVENT_READ`,
`SWOOLE_EVENT_WRITE` or `SWOOLE_EVENT_READ | SWOOLE_EVENT_WRITE`

### Async IO

Swoole's Async IO provides the ability to read/write files and lookup dns records asynchronously. The following
are signatures for these functions:


```php
bool swoole_async_readfile(string $filename, mixed $callback);
bool swoole_async_writefile('test.log', $file_content, mixed $callback);
bool swoole_async_read(string $filename, mixed $callback, int $trunk_size = 8192);
bool swoole_async_write(string $filename, string $content, int $offset = -1, mixed $callback = NULL);
void swoole_async_dns_lookup(string $domain, function($host, $ip){});
bool swoole_timer_after($after_n_ms, mixed $callback);
bool swoole_timer_tick($n_ms, mixed $callback);
bool swoole_timer_clear($n_ms, mixed $callback);
``` 

Refer [API Reference](http://wiki.swoole.com/wiki/page/183.html) for more detail information of these functions.


### Client

Swoole also provides a Client component to build tcp/udp clients in both asynchronous and synchronous ways.
Swoole uses the `swoole_client` class to expose all its functionalities.

synchronous blocking:
```php
$client = new swoole_client(SWOOLE_SOCK_TCP);
if (!$client->connect('127.0.0.1', 9501, 0.5))
{
    die("connect failed.");
}

if (!$client->send("hello world"))
{
    die("send failed.");
}

$data = $client->recv();
if (!$data)
{
    die("recv failed.");
}

$client->close();

```

asynchronous nonblocking:

```php
$client = new swoole_client(SWOOLE_SOCK_TCP, SWOOLE_SOCK_ASYNC);

$client->on("connect", function($cli) {
    $cli->send("hello world\n");
});
$client->on("receive", function($cli, $data){
    echo "Received: ".$data."\n";
});
$client->on("error", function($cli){
    echo "Connect failed\n";
});
$client->on("close", function($cli){
    echo "Connection close\n";
});

$client->connect('127.0.0.1', 9501, 0.5);
```

The following methods are available in swoole_client:

```php
swoole_client::__construct(int $sock_type, int $is_sync = SWOOLE_SOCK_SYNC, string $key);
int swoole_client::on(string $event, mixed $callback);
bool swoole_client::connect(string $host, int $port, float $timeout = 0.1, int $flag = 0)
bool swoole_client::isConnected();
int swoole_client::send(string $data);
bool swoole_client::sendfile(string $filename)
string swoole_client::recv(int $size = 65535, bool $waitall = 0);
bool swoole_client::close();
```

Refer [API Reference](http://wiki.swoole.com/wiki/page/3.html) for more detail information of these functions.


## API Reference

* [中文](http://wiki.swoole.com/) 
* [English](https://github.com/matyhtf/swoole_doc/blob/master/docs/en/index.md) (will be ready soon)

## Related Projects

* [SwooleFramework](https://github.com/swoole/framework) Web framework powered by Swoole

## Contribution

Your contribution to Swoole development is very welcome!

You may contribute in the following ways:

* [Repost issues and feedback](https://github.com/swoole/swoole-src/issues)
* Submit fixes, features via Pull Request
* Write/polish documentation

## License

Apache License Version 2.0 see http://www.apache.org/licenses/LICENSE-2.0.html
