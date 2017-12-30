#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include <string>

#define MemoryManagerTrashingOptions \
	TO(NONE,                                                                0, 0xFF) \
	TO(ON_INITIALIZATION,                                              1 << 0, 0xCD) \
	TO(ON_ALLOCATION,                                                  1 << 1, 0xAA) \
	TO(ON_DEALLOCATION,                                                1 << 2, 0xDD) \
	TO(ON_ALL,            ON_INITIALIZATION | ON_ALLOCATION | ON_DEALLOCATION, 0xFF) \

class cMemoryManager
{
public:
	enum class eTrashing
	{
#define TO(ID, MASK, HEX_VALUE) ID = MASK,
		MemoryManagerTrashingOptions
#undef TO
	};

	cMemoryManager(unsigned int bytes, eTrashing trashing = eTrashing::ON_ALL);
	~cMemoryManager();

	void *allocate(unsigned int bytes);

	std::string dump(unsigned int bytesPerRow = 16) const;

private:
	enum class eTrashingValue
	{
#define TO(ID, MASK, HEX_VALUE) ID = HEX_VALUE,
		MemoryManagerTrashingOptions
#undef TO
	};

	unsigned int m_freeBytesCount;
	unsigned int m_totalBytesCount;
	unsigned char *m_memory;

	eTrashing m_trashing;
};

#endif
