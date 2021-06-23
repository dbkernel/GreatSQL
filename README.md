# 关于 GreatSQL
--- 

GreatSQL是源于Percona Server的分支版本，除了Percona Server已有的稳定可靠、高效、管理更方便等优势外，特别是进一步提升了MGR（MySQL Group Replication）的性能及可靠性，以及众多bug修复。

GreatSQL可以作为MySQL或Percona Server的可选替代方案，用于线上生产环境。

GreatSQL完全免费并兼容MySQL或Percona Server。


# 版本特性
---
GreatSQL版本进一步提升了MGR的性能及可靠性，并修复了众多bug。主要有以下几点特性：

- 提升大事务并发性能及稳定性
- 优化MGR队列garbage collect机制、改进流控算法，以及减少每次发送数据量，避免性能抖动
- 解决了AFTER模式下，存在节点加入集群时容易出错的问题
- 在AFTER模式下，强一致性采用多数派原则，以适应网络分区的场景
- 当MGR节点崩溃时，能更快发现节点异常状态，有效减少切主和异常节点的等待时间
- 优化MGR DEBUG日志输出格式
- 修复了可能导致数据丢失、性能抖动等多个缺陷/bug问题

# 注意事项
---
运行GreatSQL需要依赖jemalloc库，因此请先先安装上
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
就可以正常启动GreatSQL服务了。


# 版本历史
---
- [GreatSQL 更新说明 8.0.22(2021-4-1)](https://gitee.com/GreatSQL/GreatSQL/blob/master/relnotes/changes-greatsql-8-0-22.md)
- [GreatSQL 更新说明 8.0.22 v20210410(2021-4-10)](https://gitee.com/GreatSQL/GreatSQL/blob/master/relnotes/changes-greatsql-8-0-22-v20210410.md)



# 使用文档
---
- [利用GreatSQL部署MGR集群](https://gitee.com/GreatSQL/GreatSQL/wikis/%E5%88%A9%E7%94%A8GreatSQL%E9%83%A8%E7%BD%B2MGR%E9%9B%86%E7%BE%A4%EF%BC%8C%E5%B9%B6%E5%AE%8C%E6%88%90%E6%B7%BB%E5%8A%A0%E6%96%B0%E8%8A%82%E7%82%B9%20%E3%80%81%E6%BB%9A%E5%8A%A8%E5%8D%87%E7%BA%A7%E3%80%81%E5%88%87%E4%B8%BB?sort_id=4163523)
- [MySQL InnoDB Cluster+GreatSQL部署MGR集群](https://gitee.com/GreatSQL/GreatSQL/wikis/MySQL%20InnoDB%20Cluster+GreatSQL%E5%BF%AB%E9%80%9F%E9%83%A8%E7%BD%B2MGR%E9%9B%86%E7%BE%A4%EF%BC%8C%E5%B9%B6%E5%AE%9E%E7%8E%B0%E8%AF%BB%E5%86%99%E5%88%86%E7%A6%BB%E5%92%8C%E6%95%85%E9%9A%9C%E8%87%AA%E5%8A%A8%E8%BD%AC%E7%A7%BB?sort_id=4163532)
- [MySQL MGR专栏文章](https://mp.weixin.qq.com/mp/homepage?__biz=MjM5NzAzMTY4NQ==&hid=16&sn=9d3d21966d850dcf158e5b676d9060ed&scene=18#wechat_redirect)
- [MGR优化配置参考](https://gitee.com/GreatSQL/GreatSQL/wikis/MGR%E6%9C%80%E4%BD%B3%E9%85%8D%E7%BD%AE%E5%8F%82%E8%80%83?sort_id=4163506)


# 问题反馈
---
- [问题反馈 gitee](https://gitee.com/GreatSQL/GreatSQL/issues)


# 联系我们
---

扫码关注微信公众号

![输入图片说明](https://images.gitee.com/uploads/images/2021/0322/093319_38b5ef38_8779455.jpeg "greatdb微信公众号二维码.jpg")
