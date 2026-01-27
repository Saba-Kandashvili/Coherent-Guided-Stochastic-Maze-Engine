#ifndef __cgsme_UTILS_H__
#define __cgsme_UTILS_H__

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include "cgsme_debug.h"

// --- QUEUE FOR FLOOD FILL ---
typedef struct
{
    int32_t x, y;
} Point2D;
typedef struct
{
    Point2D *data;
    int head, tail, cap;
} Queue2D;

/// @brief Initialize a queue with given capacity.
/// @param cap Maximum number of elements.
/// @return Pointer to the newly allocated Queue2D.
Queue2D *q_init(int cap);

/// @brief Push a point onto the queue.
/// @param q Pointer to the queue.
/// @param x X coordinate.
/// @param y Y coordinate.
void q_push(Queue2D *q, int x, int y);

/// @brief Pop a point from the queue.
/// @param q Pointer to the queue.
/// @param out Output point.
/// @return 1 if successful, 0 if queue is empty.
int q_pop(Queue2D *q, Point2D *out);

/// @brief Free the queue memory.
/// @param q Pointer to the queue to free.
void q_free(Queue2D *q);

// MIN-HEAP OPTIMIZATION

typedef struct
{
    uint32_t x;
    uint32_t y;
    float score; // Cached score (Entropy + Distance + Noise)
} HeapNode;

typedef struct
{
    HeapNode *nodes;   // The binary heap array
    int32_t *indexMap; // lookup table: map[y * width + x] = heap_index (or -1)
    uint32_t count;
    uint32_t capacity;
    uint32_t width; // for index calculation
    uint32_t length;
} MinHeap;

/// @brief Initialize a min-heap for the given grid dimensions.
/// @param width Grid width.
/// @param length Grid length.
/// @return Pointer to the newly allocated MinHeap.
MinHeap *initHeap(uint32_t width, uint32_t length);

/// @brief Free the heap memory.
/// @param h Pointer to the heap to free.
void freeHeap(MinHeap *h);

/// @brief Swap two nodes in the heap and update the index map.
/// @param h Pointer to the heap.
/// @param i First node index.
/// @param j Second node index.
void swapNodes(MinHeap *h, uint32_t i, uint32_t j);

/// @brief Bubble up a node to restore heap property.
/// @param h Pointer to the heap.
/// @param index Index of the node to bubble up.
void bubbleUp(MinHeap *h, uint32_t index);

/// @brief Bubble down a node to restore heap property.
/// @param h Pointer to the heap.
/// @param index Index of the node to bubble down.
void bubbleDown(MinHeap *h, uint32_t index);

/// @brief Insert or update a node in the heap.
/// @param h Pointer to the heap.
/// @param grid Pointer to the grid layer.
/// @param x X coordinate.
/// @param y Y coordinate.
/// @param distMap Distance map for score calculation.
/// @param rng Pointer to random state.
void heapInsertOrUpdate(MinHeap *h, uint16_t **grid, uint32_t x, uint32_t y, float **distMap, uint32_t *rng);

/// @brief Pop the minimum node from the heap.
/// @param h Pointer to the heap.
/// @param grid Pointer to the grid layer.
/// @param outX Output X coordinate.
/// @param outY Output Y coordinate.
/// @return true if a valid node was found, false if heap is empty.
bool heapPop(MinHeap *h, uint16_t **grid, uint32_t *outX, uint32_t *outY);

#endif // __cgsme_UTILS_H__