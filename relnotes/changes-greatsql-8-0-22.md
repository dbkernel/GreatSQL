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