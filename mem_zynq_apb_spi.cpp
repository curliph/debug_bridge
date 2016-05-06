#include "mem_zynq_apb_spi.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#define PULP_MEM_BASE    0x51070000
#define PULP_MEM_SIZE    0x10000

#define SPI_DEFAULT_CLKDIV 4
#define SPI_DEFAULT_DUMMYCYCLES 33

#define SPI_STD     0x0
#define SPI_QUAD_TX 0x1
#define SPI_QUAD_RX 0x2

#define SPI_TRANS_RD  0x1
#define SPI_TRANS_WR  0x2
#define SPI_TRANS_QRD 0x4
#define SPI_TRANS_QWR 0x8

#define SPI_STATUS 0x00
#define SPI_CLKDIV 0x04
#define SPI_CMD    0x08
#define SPI_ADDR   0x0c
#define SPI_LEN    0x10
#define SPI_DUMMY  0x14
#define SPI_TXFIFO 0x18
#define SPI_RXFIFO 0x20
#define SPI_INTCFG 0x24
#define SPI_INTSTA 0x28

#define SPI_CMD_RD  0
#define SPI_CMD_WR  1
#define SPI_CMD_QRD 2
#define SPI_CMD_QWR 3


ZynqAPBSPIIF::ZynqAPBSPIIF() {
  m_qpi_enabled = false;

  g_mem_dev = ::open("/dev/mem", O_RDWR | O_SYNC);
  if(g_mem_dev == -1) {
    printf("mmap_gen: Opening /dev/mem failed\n");
    exit(1);
  }

  if(::flock(g_mem_dev, LOCK_NB | LOCK_EX)) {
     printf("Error %02x\n", errno);
     printf("/dev/mem is probably locked by another application.\n");
     close(g_mem_dev);
     exit(1);
  }

  if (mmap_gen(PULP_MEM_BASE, PULP_MEM_SIZE, &m_virt_apbspi) < 0) {
    printf("Unable to open APB SPI device\n");
    exit(1);
  }

  const uint32_t mem_address = 0xf8007000;
  const uint32_t mem_size = 0x200;
  if(mmap_gen(mem_address, mem_size, &m_virt_status) < 0) {
    printf("Unable to open status device\n");
    exit(1);
  }

  if(!is_fpga_programmed()) {
    printf("Fatal: the FPGA is not programmed.\n");
    exit(1);
  }

  set_clkdiv(SPI_DEFAULT_CLKDIV);
  set_dummycycles(SPI_DEFAULT_DUMMYCYCLES);

  printf("Zynq APB SPI interface initialized\n");
}

ZynqAPBSPIIF::~ZynqAPBSPIIF() {
  close(g_mem_dev);
}


int
ZynqAPBSPIIF::mmap_gen(
  uint32_t mem_address,
  uint32_t mem_size,
  volatile uint32_t **return_ptr
) {
  uint32_t alloc_mem_size, page_mask, page_size;
  volatile char *mem_ptr, *virt_ptr;
  volatile uint32_t *uint_ptr;

  int mem_dev = ::open("/dev/mem", O_RDWR | O_SYNC);
  if(mem_dev == -1) {
    printf ("mmap_gen: Opening /dev/mem failed\n");
    return -1;
  }

  page_size = sysconf(_SC_PAGESIZE);
  alloc_mem_size = (((mem_size / page_size) + 1) * page_size);
  page_mask = (page_size - 1);

  if (page_size == -1) {
    printf("mmap_gen: sysconf failed to get page size\n");
    return -4;
  }

  mem_ptr = (char*)::mmap(NULL,
                 alloc_mem_size,
                 PROT_READ | PROT_WRITE,
                 MAP_SHARED,
                 mem_dev,
                 (mem_address & ~page_mask));

  if (mem_ptr == MAP_FAILED) {
    printf("mmap_gen: map failed\n");
    return -2;
  }

  virt_ptr = (mem_ptr + (mem_address & page_mask));
  uint_ptr = (volatile uint32_t *) virt_ptr;
  *return_ptr = uint_ptr;

  close(mem_dev);

  return 0;
}


int
ZynqAPBSPIIF::is_fpga_programmed() {
  return (m_virt_status[0xC >> 2] & 0x4) >> 2;
}

void
ZynqAPBSPIIF::set_clkdiv(uint32_t clkdiv) {
  // set clk divider to clkdiv
  apb_write(SPI_CLKDIV, clkdiv);
}

void
ZynqAPBSPIIF::set_dummycycles(uint32_t dummycycles) {
  // set dummy cycles to dummycycles
  apb_write(SPI_DUMMY, dummycycles);
}

void
ZynqAPBSPIIF::qpi_enable(bool enable) {
  if (enable == m_qpi_enabled) {
    printf ("QPI is already enabled. Doing nothing\n");
    return;
  }

  if (enable) {
   // write command 1, content 1
   apb_write(SPI_CMD, 0x01010000);
   // write length (16 cmd bits)
   apb_write(SPI_LEN, 0x00000010);
   // start transfer in SPI mode
   apb_write(SPI_STATUS, (0x1 << 8) | SPI_TRANS_WR);
   // poll over completion register
   while(apb_read(SPI_STATUS) != 1);

  } else {
   // write command 1, content 1
   apb_write(SPI_CMD, 0x01000000);
   // write length (16 cmd bits)
   apb_write(SPI_LEN, 0x00000010);
   // start transfer in SPI mode
   apb_write(SPI_STATUS, (0x1 << 8) | SPI_TRANS_QWR);
   // poll over completion register
   while(apb_read(SPI_STATUS) != 1);
  }

  m_qpi_enabled = enable;
}

bool
ZynqAPBSPIIF::access(bool write, unsigned int addr, int size, char* buffer) {
  if (write)
    return mem_write(addr, size, buffer);
  else
    return mem_read(addr, size, buffer);
}

bool
ZynqAPBSPIIF::mem_read(unsigned int addr, int len, char *src) {
  char* buffer = (char*)malloc(len + 8); // +8 to have enough space if both start and end are not aligned

  unsigned int addr_int = addr;
  unsigned int len_int  = len;

  while (addr_int % 4 != 0) {
    addr_int--;
    len_int++;
  }

  len_int = len_int + (4 - (len_int % 4));


  // now len_int and addr_int are aligned to words
  // so we can start the burst transfer
  mem_read_words(addr_int, len_int, buffer);

  // and after that we just copy our received buffer
  memcpy(src, buffer + (addr_int - addr), len);

  free(buffer);

  return true;
}

bool
ZynqAPBSPIIF::mem_write(unsigned int addr, int len, char *src) {
  char rdata[4];

  // first take care of aligning the address
  if (addr % 4 != 0) {
    mem_read_words(addr & 0xFFFFFFFC, 4, rdata);
    unsigned int addr_int = addr;

    for (int i = addr_int % 4; i < 4 && len >= 0; i++) {
      rdata[i] = *src++;
      len--;
      addr++;
    }

    mem_write_words(addr_int & 0xFFFFFFFC, 4, rdata);
  }

  // the address is aligned now, so let's write words in a burst
  int len_burst = len - (len % 4);
  mem_write_words(addr, len_burst, src);
  addr += len_burst;
  len  -= len_burst;
  src  += len_burst;


  // now the trailing alignment
  if (len > 0) {
    mem_read_words(addr & 0xFFFFFFFC, 4, rdata);
    unsigned int addr_int = addr;

    for (int i = 0; i < len; i++) {
      rdata[i] = *src++;
      len--;
      addr++;
    }

    mem_write_words(addr_int & 0xFFFFFFFC, 4, rdata);
  }

  return true;
}

// this function can only write word-aligned data
// Assumptions: len % 4 == 0 && addr % 4 == 0
void
ZynqAPBSPIIF::mem_write_words(unsigned int addr, int len, char *src) {
  if (len % 4 != 0 || addr % 4 != 0) {
    printf ("mem_write_words: Illegal input values; not word-aligned\n");
    return;
  }

  bool qpi_was_enabled = m_qpi_enabled;

  if (!qpi_was_enabled)
    qpi_enable(true);

  size_t len_rem = len;
  uint32_t* src_int = (uint32_t*)src;

  apb_write(SPI_CMD, 0x02000000); // command: TX QPI mode
  apb_write(SPI_ADDR, addr);
  apb_write(SPI_LEN, ((len << 3) << 16) | 0x2008); // write_length (len*8 data bits, 32 addr bits, 8 cmd bits)

  // fill FIFO (before start of transfer)
  uint32_t len_int = (len < 16) ? len : 16;
  for(int j = 0; j < (len_int >> 2); j++) {
    uint32_t status;
    do {
      status = apb_read(SPI_STATUS);
    } while((status >> 24) >= 8);

    apb_write(SPI_TXFIFO, *src_int++);
  }

  // start transfer
  apb_write(SPI_STATUS, (0x1 << 8) | SPI_TRANS_QWR);

  // continue filling FIFO (after start of transfer)
  if(len > 16) {
    int count;
    for(int j = 4; j < (len >> 2); j++) {
      uint32_t status;
      do {
        status = apb_read(SPI_STATUS);
      } while((status >> 24) >= 8);

      apb_write(SPI_TXFIFO, *src_int++);
    }
  }

  // wait for end-of-transfer
  while((apb_read(SPI_STATUS) & 0xffff) != 1);

  if (!qpi_was_enabled)
    qpi_enable(false);
}

void
ZynqAPBSPIIF::mem_read_words(unsigned int addr, int len, char *src) {
  if (len % 4 != 0 || addr % 4 != 0) {
    printf ("mem_read_words: Illegal input values; not word-aligned\n");
    return;
  }

  uint32_t* src_int = (uint32_t*)src;
  bool qpi_was_enabled = m_qpi_enabled;

  if (!qpi_was_enabled)
    qpi_enable(true);

  set_dummycycles(SPI_DEFAULT_DUMMYCYCLES);

  apb_write(SPI_CMD, 0x0b000000); // command: B (RX QPI mode)
  apb_write(SPI_ADDR, addr);
  apb_write(SPI_LEN, ((len << 3) << 16) | 0x2008); // write_length (len_int*8 data bits, 32 addr bits, 8 cmd bits)

  // start transfer
  apb_write(SPI_STATUS, (0xf << 8) | SPI_TRANS_QRD);

  // wait for end-of-transfer
  while((apb_read(SPI_STATUS) & 0xffff) != 1);

  // continuosly empty FIFO (after start of transfer)
  for(int j = 0; j < (len>>2); j++) {
     while((apb_read(SPI_STATUS) >> 24) >= 8);
     *src_int++ = apb_read(SPI_RXFIFO);
  }

  if (!qpi_was_enabled)
    qpi_enable(false);
}
