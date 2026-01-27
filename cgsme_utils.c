#include "cgsme_utils.h"
#include "cgsme_debug.h"
#include "cgsme_solver.h"
#include <stdio.h>
#include <math.h>

Queue2D *q_init(int cap)
{
	Queue2D *q = malloc(sizeof(Queue2D));
	q->data = malloc(sizeof(Point2D) * cap);
	q->head = q->tail = 0;
	q->cap = cap;
	return q;
}
void q_push(Queue2D *q, int x, int y)
{
	if (q->tail < q->cap)
	{
		q->data[q->tail].x = x;
		q->data[q->tail].y = y;
		q->tail++;
	}
}
int q_pop(Queue2D *q, Point2D *out)
{
	if (q->head >= q->tail)
		return 0;
	*out = q->data[q->head++];
	return 1;
}
void q_free(Queue2D *q)
{
	free(q->data);
	free(q);
}

MinHeap *initHeap(uint32_t width, uint32_t length)
{
	CGSME_PROFILE_FUNC();
	MinHeap *h = malloc(sizeof(MinHeap));
	h->capacity = width * length; // Worst case: everything added
	h->count = 0;
	h->width = width;
	h->length = length;
	h->nodes = malloc(sizeof(HeapNode) * h->capacity);

	// Initialize map to -1 (not in heap)
	h->indexMap = malloc(sizeof(int32_t) * width * length);
	for (uint32_t i = 0; i < width * length; i++)
		h->indexMap[i] = -1;

	return h;
}

void freeHeap(MinHeap *h)
{
	CGSME_PROFILE_FUNC();
	free(h->nodes);
	free(h->indexMap);
	free(h);
}

void swapNodes(MinHeap *h, uint32_t i, uint32_t j)
{
	CGSME_PROFILE_FUNC();
	HeapNode temp = h->nodes[i];
	h->nodes[i] = h->nodes[j];
	h->nodes[j] = temp;

	// Update the lookup map!
	uint32_t idx_i = h->nodes[i].y * h->width + h->nodes[i].x;
	uint32_t idx_j = h->nodes[j].y * h->width + h->nodes[j].x;
	h->indexMap[idx_i] = i;
	h->indexMap[idx_j] = j;
}

void bubbleUp(MinHeap *h, uint32_t index)
{
	CGSME_PROFILE_FUNC();
	while (index > 0)
	{
		uint32_t parent = (index - 1) / 2;
		if (h->nodes[index].score < h->nodes[parent].score)
		{
			swapNodes(h, index, parent);
			index = parent;
		}
		else
		{
			break;
		}
	}
}

void bubbleDown(MinHeap *h, uint32_t index)
{
	CGSME_PROFILE_FUNC();
	while (true)
	{
		uint32_t left = 2 * index + 1;
		uint32_t right = 2 * index + 2;
		uint32_t smallest = index;

		if (left < h->count && h->nodes[left].score < h->nodes[smallest].score)
			smallest = left;
		if (right < h->count && h->nodes[right].score < h->nodes[smallest].score)
			smallest = right;

		if (smallest != index)
		{
			swapNodes(h, index, smallest);
			index = smallest;
		}
		else
		{
			break;
		}
	}
}

// Adds a node or Updates it if it already exists
void heapInsertOrUpdate(MinHeap *h, uint16_t **grid, uint32_t x, uint32_t y, float **distMap, uint32_t *rng)
{
	CGSME_PROFILE_FUNC();
	uint32_t mapIdx = y * h->width + x;
	int32_t currentHeapIdx = h->indexMap[mapIdx];

	// Check validity
	if (x >= h->width || y >= h->length)
		return;
	// don't add collapsed tiles (1 bit) or broken tiles (0 bits)
	if (__builtin_popcount(grid[y][x]) <= 1)
	{
		// IF it was in heap, remove it (lazy removal happens on pop usually, but we can do logic here if strictly needed)
		// for WFC, typically once collapsed we just ignore it.
		// IF it IS in the heap (rare race condition logic), we leave it, it will be popped and ignored.
		return;
	}

	// Calculate fresh score
	float score = calculateScore(grid, x, y, distMap, rng);

	if (currentHeapIdx != -1)
	{
		// ALREADY IN HEAP: Update score
		// entropy only ever decreases, so score only decreases. just bubble up.
		if (score < h->nodes[currentHeapIdx].score)
		{
			h->nodes[currentHeapIdx].score = score;
			bubbleUp(h, currentHeapIdx);
		}
	}
	else
	{
		// NEW INSERTION
		uint32_t idx = h->count;
		h->nodes[idx].x = x;
		h->nodes[idx].y = y;
		h->nodes[idx].score = score;
		h->indexMap[mapIdx] = idx;
		h->count++;
		bubbleUp(h, idx);
	}
}

// returns true if valid node found, false if empty
bool heapPop(MinHeap *h, uint16_t **grid, uint32_t *outX, uint32_t *outY)
{
	CGSME_PROFILE_FUNC();
	while (h->count > 0)
	{
		// take top
		HeapNode top = h->nodes[0];

		// remove top (swap with last, decrease count)
		uint32_t lastIdx = h->count - 1;
		swapNodes(h, 0, lastIdx);
		h->count--;
		h->indexMap[top.y * h->width + top.x] = -1; // Mark as removed

		if (h->count > 0)
			bubbleDown(h, 0);

		// validation: actually uncollapsed?
		// it's possible we added it to heap, then later it got collapsed by something else?
		// (Unlikely in single thread, but good safety).
		if (__builtin_popcount(grid[top.y][top.x]) > 1)
		{
			*outX = top.x;
			*outY = top.y;
			return true;
		}
		// IF not valid, loop again (Lazy Deletion)
	}
	return false;
}