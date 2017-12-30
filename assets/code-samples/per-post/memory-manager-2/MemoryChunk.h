#ifndef MEMORY_CHUNK_H
#define MEMORY_CHUNK_H

struct cMemoryChunk
{
	bool m_isInUse;
	unsigned int m_bytes;

	cMemoryChunk *m_previous;
	cMemoryChunk *m_next;

	cMemoryChunk(unsigned int bytes) :
		m_bytes(bytes),
		m_isInUse(false),
		m_previous(nullptr),
		m_next(nullptr)
	{
	}
};

#endif
