# 关于 GreatSQL
--- 

GreatSQL是源于Percona Server的分支版本，除了Percona Server已有的稳定可靠、高效、管理更方便等优势外，特别是进一步提升了MGR（MySQL Group Replication）的性能及可靠性，以及众多bug修复。此外，GreatSQL还合并了由华为鲲鹏计算团队贡献的两个Patch，分别针对OLTP和OLAP两种业务场景，尤其是InnoDB并行查询特性，TPC-H测试中平均提升聚合分析型SQL性能15倍，最高提升40多倍，特别适用于周期性数据汇总报表之类的SAP、财务统计等业务。

GreatSQL可以作为MySQL或Percona Server的可选替代方案，用于线上生产环境。

GreatSQL完全免费并兼容MySQL或Percona Server。


# 版本特性
---
GreatSQL除了提升MGR性能及可靠性，还引入InnoDB事务锁优化及并行查询优化等特性，以及众多BUG修复。
选用GreatSQl主要有以下几点优势：

- 提升MGR模式下的大事务并发性能及稳定性
- 改进MGR的GC及流控算法，以及减少每次发送数据量，避免性能抖动
- 在MGR集群AFTER模式下，解决了节点加入集群时容易出错的问题
- 在MGR集群AFTER模式下，强一致性采用多数派原则，以适应网络分区的场景
- 当MGR节点崩溃时，能更快发现节点异常状态，有效减少切主和异常节点的等待时间
- 优化InnoDB事务锁机制，在高并发场景中有效提升事务并发性能至少10%以上
- 实现InnoDB并行查询机制，极大提升聚合查询效率，TPC-H测试中，最高可提升40多倍，平均提升15倍。特别适用于周期性数据汇总报表之类的SAP、财务统计等业务
- 修复了MGR模式下可能导致数据丢失、性能抖动、节点加入恢复极慢等多个缺陷或BUG

# 注意事项
---
运行GreatSQL可能需要依赖jemalloc库，因此请先先安装上
```
yum -y install jemalloc jemalloc-devel
```
也可以把自行安装的lib库so文件路径加到系统配置文件中，例如：
```
[root@greatdb]# cat /etc/ld.so.conf
/usr/local/lib64/
```
而后执行下面的操作加载libjemalloc库，并确认是否已存在
```
[root@greatdb]# ldconfig

[root@greatdb]# ldconfig -p | grep libjemalloc
        libjemalloc.so.1 (libc6,x86-64) => /usr/local/lib64/libjemalloc.so.1
        libjemalloc.so (libc6,x86-64) => /usr/local/lib64/libjemalloc.so
```

my.cnf配置文件可参考[这份样例](https://gitee.com/GreatSQL/GreatSQL/blob/master/docs/my.cnf-example)。

就可以正常启动GreatSQL服务了。


# 版本历史
---
- [GreatSQL 更新说明 8.0.22(2021-4-1)](https://gitee.com/GreatSQL/GreatSQL/blob/master/relnotes/changes-greatsql-8-0-22.md)
- [GreatSQL 更新说明 8.0.22 v20210410(2021-4-10)](https://gitee.com/GreatSQL/GreatSQL/blob/master/relnotes/changes-greatsql-8-0-22-v20210410.md)
- [GreatSQL 更新说明 8.0.25(2021-8-20)](https://gitee.com/GreatSQL/GreatSQL/blob/master/relnotes/changes-greatsql-8-0-25-20210820.md)



# 使用文档
---
- [在Linux下源码编译安装GreatSQL](https://gitee.com/GreatSQL/GreatSQL/blob/master/docs/build-greatsql-with-source.md)
- [利用Ansible安装GreatSQL并构建MGR集群](https://gitee.com/GreatSQL/GreatSQL/blob/master/docs/install-greatsql-with-ansible.md)
- [在Docker中部署GreatSQL并构建MGR集群](https://gitee.com/GreatSQL/GreatSQL/blob/master/docs/install-greatsql-with-docker.md)
- [MGR优化配置参考](https://gitee.com/GreatSQL/GreatSQL/wikis/MGR%E6%9C%80%E4%BD%B3%E9%85%8D%E7%BD%AE%E5%8F%82%E8%80%83?sort_id=4163506)
- [InnoDB并行查询优化参考](https://gitee.com/GreatSQL/GreatSQL/blob/master/docs/innodb-parallel-execute.md)
- [利用GreatSQL部署MGR集群](https://gitee.com/GreatSQL/GreatSQL/wikis/%E5%88%A9%E7%94%A8GreatSQL%E9%83%A8%E7%BD%B2MGR%E9%9B%86%E7%BE%A4%EF%BC%8C%E5%B9%B6%E5%AE%8C%E6%88%90%E6%B7%BB%E5%8A%A0%E6%96%B0%E8%8A%82%E7%82%B9%20%E3%80%81%E6%BB%9A%E5%8A%A8%E5%8D%87%E7%BA%A7%E3%80%81%E5%88%87%E4%B8%BB?sort_id=4163523)
- [MySQL InnoDB Cluster+GreatSQL部署MGR集群](https://gitee.com/GreatSQL/GreatSQL/wikis/MySQL%20InnoDB%20Cluster+GreatSQL%E5%BF%AB%E9%80%9F%E9%83%A8%E7%BD%B2MGR%E9%9B%86%E7%BE%A4%EF%BC%8C%E5%B9%B6%E5%AE%9E%E7%8E%B0%E8%AF%BB%E5%86%99%E5%88%86%E7%A6%BB%E5%92%8C%E6%95%85%E9%9A%9C%E8%87%AA%E5%8A%A8%E8%BD%AC%E7%A7%BB?sort_id=4163532)
- [MySQL MGR专栏文章](https://mp.weixin.qq.com/mp/homepage?__biz=MjM5NzAzMTY4NQ==&hid=16&sn=9d3d21966d850dcf158e5b676d9060ed&scene=18#wechat_redirect)

# 相关资源
- [GreatSQL-Docker](https://gitee.com/GreatSQL/GreatSQL-Docker)，在Docker中运行GreatSQL。
- [GreatSQL-Ansible](https://gitee.com/GreatSQL/GreatSQL-Ansible)，利用ansible一键安装GreatSQL并完成MGR集群部署。

# 问题反馈
---
- [问题反馈 gitee](https://gitee.com/GreatSQL/GreatSQL/issues)


# 联系我们
---

扫码关注微信公众号

![输入图片说明](https://images.gitee.com/uploads/images/2021/0802/141935_2ea2c196_8779455.jpeg "greatsql社区-wx-qrcode-0.5m.jpg")
