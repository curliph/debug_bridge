
#include "debug_if.h"
#include "mem.h"

#include <stdio.h>

DbgIF::DbgIF(MemIF* mem, unsigned int base_addr) {
  this->m_mem = mem;
  this->m_base_addr = base_addr;
}

bool
DbgIF::write(uint32_t addr, uint32_t wdata) {
  return m_mem->access(1, m_base_addr + addr, 4, (char*)&wdata);
}

bool
DbgIF::read(uint32_t addr, uint32_t* rdata) {
  return m_mem->access(0, m_base_addr + addr, 4, (char*)rdata);
}

bool
DbgIF::halt() {
  uint32_t data;
  if (!this->read(DBG_CTRL_REG, &data)) {
    fprintf(stderr, "debug_is_stopped: Reading from CTRL reg failed\n");
    return false;
  }

  data |= 0x1 << 16;
  return this->write(DBG_CTRL_REG, data);
}

bool
DbgIF::is_stopped() {
  uint32_t data;
  if (!this->read(DBG_CTRL_REG, &data)) {
    fprintf(stderr, "debug_is_stopped: Reading from CTRL reg failed\n");
    return false;
  }

  if (data & 0x10000)
    return true;
  else
    return false;
}

bool
DbgIF::gpr_read_all(uint32_t *data) {
  return m_mem->access(0, m_base_addr + 0x1000, 32 * 4, (char*)data);
}

bool
DbgIF::gpr_read(unsigned int i, uint32_t *data) {
  return this->read(0x1000 + i * 4, data);
}

bool
DbgIF::gpr_write(unsigned int i, uint32_t data) {
  return this->write(0x1000 + i * 4, data);
}

bool
DbgIF::csr_read(unsigned int i, uint32_t *data) {
  return this->read(0x4000 + i * 4, data);
}

bool
DbgIF::csr_write(unsigned int i, uint32_t data) {
  return this->write(0x4000 + i * 4, data);
}
