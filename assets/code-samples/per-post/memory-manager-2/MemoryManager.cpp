#include "MemoryManager.h"

#include <sstream>
#include <iomanip>

#include "MemoryChunk.h"

cMemoryManager::cMemoryManager(unsigned int bytes, eTrashing trashing) :
	m_memory(nullptr),
	m_freeBytesCount(bytes - sizeof(cMemoryChunk)),
	m_totalBytesCount(bytes),
	m_trashing(trashing)
{
	m_memory = ::new unsigned char[bytes];

	// optional trashing
	if (static_cast<int>(m_trashing) & static_cast<int>(eTrashing::ON_INITIALIZATION))
	{
		memset(m_memory, static_cast<int>(eTrashingValue::ON_INITIALIZATION), m_totalBytesCount);
	}

	// first chunk
	new (m_memory) cMemoryChunk(m_freeBytesCount);
}

cMemoryManager::~cMemoryManager()
{
	::delete[] m_memory;
	m_memory = nullptr;
}

std::string cMemoryManager::dump(unsigned int bytesPerRow) const
{
	const unsigned char *memoryIterator = m_memory;
	const unsigned char *characterIterator = m_memory;

	std::stringstream ss;

	// iterate over the memory
	for (; static_cast<unsigned int>(memoryIterator - m_memory) < m_totalBytesCount; memoryIterator += bytesPerRow)
	{
		// print left column
		ss << static_cast<const void *>(memoryIterator) << ":  ";

		// iterate over the row
		for (unsigned int charIndex = 0; charIndex < bytesPerRow; ++charIndex, ++characterIterator)
		{
			const unsigned int characterIteratorAsInt = static_cast<unsigned int>(*characterIterator);
			ss << std::uppercase << std::hex << std::setw(2) << std::setfill('0');

			// bytes in central column: general case
			if (charIndex < bytesPerRow - 1)
			{
				ss << characterIteratorAsInt << ":";
			}
			// bytes in central column: last item
			else
			{
				ss << characterIteratorAsInt << "  ";
				ss << std::nouppercase << std::dec;

				// get the string representation of the bytes in the row (right column)
				for (unsigned int representationIndex = 0; representationIndex < bytesPerRow; ++representationIndex)
				{
					const unsigned char &character = *(memoryIterator + representationIndex);
					ss << (isprint(character) ? character : static_cast<unsigned char>('.'));
				}
			}
		}

		ss << "\n";
	}

	return ss.str();
}

void *cMemoryManager::allocate(unsigned int bytes)
{
	const unsigned int chunkSize = sizeof(cMemoryChunk);

	// find the first free chunk that can store the requested number of bytes
	cMemoryChunk *chunk = reinterpret_cast<cMemoryChunk *>(m_memory);
	while (chunk != nullptr)
	{
		// need enough bytes to allocate the requested bytes and a new chunk
		if (!chunk->m_isInUse && chunk->m_bytes >= (bytes + chunkSize))
		{
			break;
		}

		chunk = chunk->m_next;
	}

	// might not be able to allocate it
	if (chunk == nullptr)
	{
		return nullptr;
	}

	unsigned char *chunkAddress = reinterpret_cast<unsigned char *>(chunk);

	// build a new chunk from the remaining space in the chunk
	unsigned char *newChunkAddress = chunkAddress + chunkSize + bytes;
	cMemoryChunk *newChunk = new (newChunkAddress) cMemoryChunk(chunk->m_bytes - bytes - chunkSize);
	newChunk->m_previous = chunk;
	newChunk->m_next = chunk->m_next;

	// link it appropriately with the next chunk, if any
	if (newChunk->m_next != nullptr)
	{
		newChunk->m_next->m_previous = newChunk;
	}

	// update the previously free chunk
	chunk->m_next = newChunk;
	chunk->m_bytes = bytes;
	chunk->m_isInUse = true;

	// update manager-level data
	m_freeBytesCount -= chunk->m_bytes + chunkSize;

	// trashing on allocation?
	if (static_cast<int>(m_trashing) & static_cast<int>(eTrashing::ON_ALLOCATION))
	{
		memset(chunkAddress + chunkSize, static_cast<int>(eTrashingValue::ON_ALLOCATION), chunk->m_bytes);
	}

	// the memory we give back to the user doesn't include the chunk itself
	return chunkAddress + chunkSize;
}