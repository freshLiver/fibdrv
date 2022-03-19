#if !defined(BIGNUM_H)
#define BIGNUM_H

#if !defined(__KERNEL__)
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tests/list.h"

#define vzalloc(s) calloc(s, 1)
#define kmalloc(s, gfp) malloc(s)
#define kfree(p) free(p)
#define vfree(p) free(p)
#define printk(...) printf(__VA_ARGS__)
#define U64_FMT "lu"
#else
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/types.h>
#define U64_FMT "llu"
#endif  // __KERNEL__

typedef struct {
    size_t len;
    struct list_head link;
} bignum_head;


typedef struct {
    uint64_t value;
    struct list_head link;
    struct list_head carries;
} bignum_node;


typedef struct {
    uint64_t value;
    struct list_head link;
} bignum_carry;


// 18,446,744,073,709,551,615
#define MAX_DIGITS 9
#define BOUND64 1000000000UL
#define LEADING_FMT "%09"

#define NEW_BIGNUM_NODE(head, val)                                    \
    ({                                                                \
        bignum_node *node = kmalloc(sizeof(bignum_node), GFP_KERNEL); \
        if (node) {                                                   \
            node->value = val;                                        \
            INIT_LIST_HEAD(&node->link);                              \
            INIT_LIST_HEAD(&node->carries);                           \
            list_entry(head, bignum_head, link)->len++;               \
            list_add_tail(&node->link, head);                         \
        }                                                             \
    })

#define FULL_ADDER_64(a, b, carry)               \
    ({                                           \
        uint64_t res = (a) + (b) + (carry);      \
        uint64_t overflow = -!!(res >= BOUND64); \
        (b) = res - (BOUND64 & overflow);        \
        -overflow;                               \
    })



#define NEW_CARRY_NODE(carry_list, val)                                      \
    ({                                                                       \
        bignum_carry *new_carry = kmalloc(sizeof(bignum_carry), GFP_KERNEL); \
        if (new_carry) {                                                     \
            new_carry->value = val;                                          \
            INIT_LIST_HEAD(&new_carry->link);                                \
            list_add_tail(&new_carry->link, carry_list);                     \
        }                                                                    \
    })

#define CARRY_HANDLER(head, pnext, carry, remain)                \
    ({                                                           \
        typeof(carry) _cry = (carry);                            \
        typeof(remain) _rem = (remain);                          \
                                                                 \
        if (*(pnext) == (head))                                  \
            NEW_BIGNUM_NODE((head), 0);                          \
        list_entry(*(pnext), bignum_node, link)->value = _rem;   \
                                                                 \
        if (_cry && ((*(pnext))->next == (head)))                \
            NEW_BIGNUM_NODE((head), 0);                          \
        if (_cry) {                                              \
            bignum_node *next =                                  \
                list_entry((*(pnext))->next, bignum_node, link); \
            NEW_CARRY_NODE(&next->carries, _cry);                \
        }                                                        \
    })



static inline struct list_head *bignum_new(uint64_t val)
{
    bignum_head *head = kmalloc(sizeof(bignum_head), GFP_KERNEL);
    INIT_LIST_HEAD(&head->link);
    NEW_BIGNUM_NODE(&head->link, val);
    head->len = 1;
    return &head->link;
}

static inline void bignum_add_to_smaller(struct list_head *lgr,
                                         struct list_head *slr)
{
    struct list_head **l = &lgr->next, **s = &slr->next;

    for (bool carry = 0;; l = &(*l)->next, s = &(*s)->next) {
        // slr don't have next node (but lgr may have)
        if (*s == slr) {
            // no more node, no carry => terminate
            if (*l == lgr) {
                if (carry)
                    NEW_BIGNUM_NODE(slr, 1);
                break;
            }

            // next node exists or carry exists => new node for slr and do add
            NEW_BIGNUM_NODE(slr, 0);
        }

        // add two node's value
        bignum_node *lentry = list_entry(*l, bignum_node, link),
                    *sentry = list_entry(*s, bignum_node, link);

        carry = FULL_ADDER_64(lentry->value, sentry->value, carry);
    }
}

static inline struct list_head *bignum_multiply(struct list_head *mtr,
                                                struct list_head *mtd)
{
    // multiply each mtd node to each mtr node O(n^2)
    struct list_head *result = bignum_new(0), **ptr = &result->next;

    for (struct list_head *pmtd = mtd->next; pmtd != mtd; pmtd = pmtd->next) {
        struct list_head *nxt = (*ptr)->next, *pmtr;
        list_for_each (pmtr, mtr) {
            // get node value and calc product and carry
            uint64_t product = list_entry(pmtr, bignum_node, link)->value *
                               list_entry(pmtd, bignum_node, link)->value,
                     carry = product / BOUND64, remain = product % BOUND64;
            // set remain and carry carry to next node
            CARRY_HANDLER(result, &nxt->next, carry, remain);
            nxt = nxt->next;
        }
        ptr = &(*ptr)->next;
    }

    // handle each node's pending carries
    for (ptr = &result->next; *ptr != result; ptr = &(*ptr)->next) {
        bignum_node *node = list_entry(*ptr, bignum_node, link);
        bignum_carry *carry = list_entry(&node->carries, bignum_carry, link),
                     *safe;
        list_for_each_entry_safe (carry, safe, &node->carries, link) {
            // add one carry from pending carry, and carry 1 if needed
            if (FULL_ADDER_64(carry->value, node->value, 0))
                CARRY_HANDLER(&node->carries, ptr, 1, 0);
            // remove carry from pending list
            list_del(&carry->link);
            kfree(carry);
        }
    }

    return result;
}

static inline char *bignum_to_string(struct list_head *head)
{
    // UINT64 < BOUND64 (10^18)
    size_t digits = list_entry(head, bignum_head, link)->len * MAX_DIGITS + 1;

    // DMA for result string
    char *res = vzalloc(digits);

    if (!res)
        return NULL;

    // decode from Most Significant Node
    if (head == head->next) {
        vfree(res);
        return NULL;
    }

    uint64_t node_result = 0;
    struct list_head *p = head->prev;

    // Most Significant Node
    node_result = list_entry(p, bignum_node, link)->value;
    snprintf(res, MAX_DIGITS + 1, "%" U64_FMT, node_result);

    // other nodes
    for (p = p->prev; p != head; p = p->prev) {
        size_t pos = strlen(res);
        node_result = list_entry(p, bignum_node, link)->value;
        snprintf(&res[pos], MAX_DIGITS + 1, LEADING_FMT U64_FMT, node_result);
    }

    return res;
}

static inline void bignum_free(struct list_head *head)
{
    bignum_node *ptr = list_entry(head, bignum_node, link), *next;
    list_for_each_entry_safe (ptr, next, head, link)
        kfree(ptr);
    kfree(list_entry(head, bignum_head, link));
}

#endif  // BIGNUM_H
