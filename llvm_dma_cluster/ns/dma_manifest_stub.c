/* 1차 링크 전용: count = 0 인 manifest (2차 링크에서는 생성된 dma_manifest.c 로 교체) */
#include "dma_manifest.h"

__attribute__((section(".dma_manifest"), used))
const struct dma_manifest g_dma_manifest = { DMA_MANIFEST_MAGIC, DMA_MANIFEST_VERSION, 0u, { { 0 } } };
