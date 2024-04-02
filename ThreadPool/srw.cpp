#include "fwd.hpp"

#if defined __linux__ && !WINE_LOCK

/* 使用链表串起等待队列，ReactOS 里面的实现
 * 自己操作链表，因此可以自定义加锁和唤醒顺序
 *
 * 太难了，还没完全看懂
 */

#endif
