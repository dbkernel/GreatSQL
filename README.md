# 关于 GreatSQL
--- 

GreatSQL是源于Percona server的分支版本，除了Percona server已有的稳定可靠、高效、管理更方便等优势外，特别是进一步提升了MGR（MySQL Group Replication）的性能及可靠性，以及众多bug修复。

GreatSQL可以作为MySQL或Percona server的可选替代方案，用于线上生产环境。

GreatSQL完全免费并兼容MySQL或Percona server。


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


# 版本历史
---
- [GreatSQL 更新说明 8.0.22(2021-4-1)](https://gitee.com/GreatSQL/GreatSQL/blob/master/relnotes/changes-greatsql-8-0-22.md)



# 使用文档
---
- [使用文档 gitee](https://gitee.com/GreatSQL/GreatSQL/wikis)


# 问题反馈
---
- [问题反馈 gitee](https://gitee.com/GreatSQL/GreatSQL/issues)


# 联系我们
---

扫码关注微信公众号

![输入图片说明](https://images.gitee.com/uploads/images/2021/0322/093319_38b5ef38_8779455.jpeg "greatdb微信公众号二维码.jpg")
