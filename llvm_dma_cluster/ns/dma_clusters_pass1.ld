/* 1차 링크용 임시 조각 (gen_layout.py 실행 전). NS 링커 스크립트 SECTIONS 안에서
 *   INCLUDE dma_clusters.ld
 * 로 불러오며, 빌드 스크립트가 이 파일을 build/ns/gen/dma_clusters.ld 로 복사한다.
 * 모든 .dma.* 를 한 곳에 모으기만 한다 (주소·guard 는 2차 링크에서 확정). */
.dma_all (NOLOAD) : ALIGN(256) { KEEP(*(.dma.*)) } > RAM

.dma_zero_table : ALIGN(4)
{
  __dma_zero_table_start = .;
  LONG(ADDR(.dma_all)) LONG(ADDR(.dma_all) + SIZEOF(.dma_all))
  __dma_zero_table_end = .;
} > FLASH
