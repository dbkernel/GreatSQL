# GreatSQL Changes 8.0.22(2021-3-20)
---

### 1.新增特性
- 新增MGR Arbitrator节点（仲裁节点）角色。该节点进参与MGR投票仲裁，不存放实际数据，也无需执行DML操作，因此可以用一般配置级别的服务器，在保证MGR可靠性的同时还能降低服务器成本。

新增参数
```sql
group_replication_arbitrator
```

若想新增一个仲裁节点，只需在配置文件中添加如下配置：
```sql
loose-group_replication_arbitrator = true
```

当集群中只剩下 Arbitrator 节点时，则会自动退出。

- 新增流控机制，可以控制MGR主从节点同步延迟阈值，当MGR主从节点因为大事务等原因延迟超过阈值时，就会触发流控机制

新增参数
```sql
group_replication_flow_control_replay_lag_behind
```
该参数默认为60s，可在线动态修改，例如：
```sql
mysql> SET GLOBAL group_replication_flow_control_replay_lag_behind = 60;
```
正常情况下，该参数无需调整。

### 2.稳定性提升
- 提升大事务并发性能及稳定性
- 优化MGR队列garbage collect机制、改进流控算法，以及减少每次发送数据量，避免性能抖动
- 解决了AFTER模式下，存在节点加入集群时容易出错的问题
- 在AFTER模式下，强一致性采用多数派原则，以适应网络分区的场景
- 当MGR节点崩溃时，能更快发现节点异常状态，有效减少切主和异常节点的等待时间


### 3.性能提升
- 提升强一致性读性能
- 网络分区情况下，优化吞吐量和等待时间
- 在类似跨城IDC部署的高延迟场景下，提升应用访问MGR的吞吐量，尽量减少网络延迟对访问性能的影响

### 4.bug修复
- 修复了节点异常时导致MGR大范围性能抖动问题
- 修复了传输大数据可能导致逻辑判断死循环问题
- 修复了启动过程中低效等待问题
- 修复了磁盘满导致吞吐量异常问题
- 修复了多写模式下可能丢数据的问题
- 修复了TCP self-connect问题

### 5.使用注意事项
- 选项```sql slave_parallel_workers```建议设置为逻辑CPU数的2倍，提高从库回放时的并发效率
- 原生MGR中的流控参数不再起作用，GreatSQL会根据队列长度、大小自动进行流控
- 某节点一旦设置成arbitrator（仲裁）节点，不能中途变更成数据节点，需要完全重建
- 在MGR节点正式拉起之前，务必设置```sql super_read_only=ON```（或者确保此时不会有人修改数据）
- group_replication_unreachable_majority_timeout，建议不要设置，否则网络分区的时候，给用户返回错误，但其它分区多数派已经提交了事务
- 出于问题诊断需要，建议设置```sql log_error_verbosity=3```
- 目前放出的二进制版本仅供CentOS 7环境使用，其他环境若有强烈需求请私信联系我们

### 6.编译参数
```shell
CFLAGS='-O2' cmake .. \
-DCMAKE_BUILD_TYPE=Release \
-DCMAKE_EXPORT_COMPILE_COMMANDS=1 \
-DCMAKE_EXE_LINKER_FLAGS="-ljemalloc" \
-DWITH_SAFEMALLOC=OFF \
-DBUILD_CONFIG=mysql_release \
-DWITH_INNODB_MEMCACHED=OFF \
-DWITH_ZLIB=system \
-DWITH_NUMA=ON \
-DWITH_ROUTER=OFF \
-DWITH_UNIT_TESTS=OFF \
-DWITH_TEST_TRACE_PLUGIN=OFF \
-DWITH_NDBCLUSTER=OFF \
-DWITH_ZLIB=bundled \
-DCMAKE_INSTALL_PREFIX=/build/greatsql/GreatSQL-8.0.22 \
-DWITH_BOOST=/build/greatsql/mysql-8.0.22/boost
```

https://github.com/session-replay-tools/MySQL

V2（from 王斌，2021-3-10）
修复了MGR如下问题：
2、mgr garbage_collect垃圾回收机制优化，减少不必要抖动
3、流控算法改造，减少性能小抖动
4、传输大数据死循环问题
5、大事务并发支持
6、节点异常导致MGR运行大范围性能抖动
7、TCP self-connect问题
8、支持部分xa问题补丁
10、减少每次发送内容，减少抖动
12、解决mgr after强一致性和view change同步问题
13、解决启动过程中低效等待问题
14、磁盘满导致吞吐量异常问题
15、单个mgr节点作为arbitrator
16、解决多写丢数据问题
17、解决after机制下，多线程同步问题
18、提升强一致性读操作性能
19、after强一致性采用多数派原则，以适应网络分区这种场景
20、网络分区情况下，优化吞吐量和等待时间
21、mgr节点崩溃时，快速发现节点异常，减少切换主和异常节点所导致的等待时间
22、在高延迟场景下（跨城部署），提升应用访问mgr的吞吐量，尽量减少网络延迟对系统的影响

新增了如下参数：
1、arbitrator
loose-group_replication_arbitrator=true
节点作为arbitrator，不存放数据，也不要操作任何dml，仅仅作为投票节点
2、流控参数
group_replication_flow_control_replay_lag_behind，默认为60s，可动态修改
尽可能去控制mgr主从节点之间的同步延迟
一般不需要修改，大事务建议调大

使用注意事项：
1、slave_parallel_workers一定要设置大，最好是cpu数量的2倍，确保从库回放不能被阻塞
2、原先流控参数一律无效
3、一旦设置成arbitrator节点，不能中途更改
4、mgr节点成为服务之前，请设置super_read_only=ON（每个mgr节点成为服务，配置mgr时不需要）
5、诊断问题需要，最好在配置文件配置log_error_verbosity=3
6、二进制版本只能供centos 7版本+使用
7、group_replication_unreachable_majority_timeout，建议不要设置，否则网络分区的时候，给用户返回错误，但其它分区多数派已经提交了事务

相比前一个版本，修复了原先版本的如下问题：
1、arbitrator节点不能支持dml操作，否则会影响整个集群操作
2、如果仅留下arbitrator节点，则退出集群
3、本地节点，不再利用keepalive机制

CFLAGS='-O2' cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DCMAKE_EXE_LINKER_FLAGS="-ljemalloc" -DWITH_SAFEMALLOC=OFF -DCMAKE_INSTALL_PREFIX=/mysql/binwang/mysql -DBUILD_CONFIG=mysql_release -DWITH_INNODB_MEMCACHED=OFF -DWITH_ZLIB=system -DWITH_NUMA=ON -DWITH_ROUTER=OFF -DWITH_UNIT_TESTS=OFF -DWITH_TEST_TRACE_PLUGIN=OFF -DWITH_NDBCLUSTER=OFF -DWITH_ZLIB=bundled -DWITH_BOOST=/mysql/binwang/mysql-8.0.22/boost




V1
修复了MGR如下问题：

1、log输出大一统,会影响性能5%左右
   
2、mgr garbage_collect垃圾回收机制优化，减少不必要抖动

3、流控算法改造，减少性能小抖动
    
4、传输大数据死循环问题
   
5、大事务并发支持

6、节点异常导致MGR运行大范围性能抖动
   
7、TCP self-connect问题
   
8、支持部分xa问题补丁

9、增加TCP keepalive机制，快速发现异常问题

10、减少每次发送内容，减少抖动

11、优化mgr各种等待时间
    
12、解决mgr after强一致性和view change同步问题  

13、解决启动过程中低效等待问题

14、磁盘满导致吞吐量异常问题
    
15、单个mgr节点作为arbitrator

16、解决多写丢数据问题

17、解决after机制下，多线程同步问题

18、提升强一致性读操作性能

19、after强一致性采用多数派原则，以适应网络分区这种场景

20、网络分区情况下，优化吞吐量和等待时间

21、mgr节点崩溃时，快速发现节点异常，减少切换主和异常节点所导致的等待时间

22、在高延迟场景下（跨城部署），提升应用访问mgr的吞吐量，尽量减少网络延迟对系统的影响



新增了如下参数：
1、arbitrator
loose-group_replication_arbitrator=true
节点作为arbitrator，不存放数据，也不要操作任何dml，仅仅作为投票节点

2、流控参数
group_replication_flow_control_replay_lag_behind，默认为60s
尽可能去控制mgr主从节点之间的同步延迟


使用注意事项：
1、slave_parallel_workers一定要设置大，最好是cpu数量的2倍，确保从库回放不能被阻塞
2、原先流控参数一律无效
3、一旦设置成arbitrator节点，不能中途更改
4、mgr节点成为服务之前，请设置super_read_only=ON（每个mgr节点成为服务，配置mgr时不需要）
5、诊断问题需要，最好在配置文件配置log_error_verbosity=3
6、二进制版本只能供centos 7版本+使用
7、group_replication_unreachable_majority_timeout设置不能短，超过用户设置超时时间



相比前一个版本，修复了原先版本的如下问题：
1、arbitrator节点不能支持dml操作，否则会影响整个集群操作
2、如果仅留下arbitrator节点，则退出集群
3、本地节点，不再利用keepalive机制

CFLAGS='-O2' cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DCMAKE_EXE_LINKER_FLAGS="-ljemalloc" -DWITH_SAFEMALLOC=OFF -DCMAKE_INSTALL_PREFIX=/mysql/binwang/mysql -DBUILD_CONFIG=mysql_release -DWITH_INNODB_MEMCACHED=OFF -DWITH_ZLIB=system -DWITH_NUMA=ON -DWITH_ROUTER=OFF -DWITH_UNIT_TESTS=OFF -DWITH_TEST_TRACE_PLUGIN=OFF -DWITH_NDBCLUSTER=OFF -DWITH_ZLIB=bundled  -DWITH_BOOST=/mysql/binwang/mysql-8.0.22/boost