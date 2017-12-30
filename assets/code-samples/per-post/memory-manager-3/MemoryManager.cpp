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

	ss << "Free bytes: " << m_freeBytesCount << "\n";
	ss << "Total bytes: " << m_totalBytesCount << "\n";

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

void cMemoryManager::deallocate(void *address)
{
	const unsigned char *addressToDeallocate = static_cast<unsigned char *>(address);

	bool isInRange = isAddressInMemoryRange(addressToDeallocate);
	if (isInRange)
	{
		cMemoryChunk *chunk = findChunkForUserMemory(addressToDeallocate);
		if (chunk != nullptr)
		{
			deallocateChunk(chunk);
		}
	}
}

bool cMemoryManager::isAddressInMemoryRange(const unsigned char *address) const
{
	// deallocating nullptr is allowed, we just do nothing
	if (address == nullptr)
	{
		return false;
	}

	// is it in range of our controlled memory?
	if (address < m_memory || address >= m_memory + m_totalBytesCount)
	{
		return false;
	}

	// is it within the first chunk header?
	if (address - sizeof(cMemoryChunk) < m_memory)
	{
		return false;
	}

	return true;
}

cMemoryChunk *cMemoryManager::findChunkForUserMemory(const unsigned char *address) const
{
	// iterate over chunks to find whether the pointer is a valid chunk
	cMemoryChunk *chunk = reinterpret_cast<cMemoryChunk *>(m_memory);
	const unsigned int chunkSize = sizeof(cMemoryChunk);

	while (chunk != nullptr)
	{
		const unsigned char *chunkAddress = reinterpret_cast<unsigned char *>(chunk);

		// is the given pointer at an invalid address (mid-chunk, mid-user memory)
		if (address < chunkAddress)
		{
			return nullptr;
		}

		// is this chunk the one that holds the memory we're given?
		if (chunkAddress + chunkSize == address)
		{
			// we found the chunk, and it must be in use so we can deallocate it!
			if (!chunk->m_isInUse)
			{
				return nullptr;
			}

			break;
		}

		chunk = chunk->m_next;
	}

	return chunk;
}

void cMemoryManager::deallocateChunk(cMemoryChunk *chunk)
{
	const unsigned int chunkSize = sizeof(cMemoryChunk);

	cMemoryChunk *chunkToStartDeallocation = chunk;
	cMemoryChunk *previousChunk = chunk->m_previous;
	cMemoryChunk *nextChunk = chunk->m_next;
	unsigned int newChunkFreeBytes = chunk->m_bytes;

	// update total free bytes count
	m_freeBytesCount += chunk->m_bytes;

	// got a free chunk before it?
	if (previousChunk != nullptr && !previousChunk->m_isInUse)
	{
		chunkToStartDeallocation = previousChunk;
		previousChunk = previousChunk->m_previous;

		// we're using the bytes from the previous chunk
		newChunkFreeBytes += chunkToStartDeallocation->m_bytes;

		// also merging the header of the given chunk
		newChunkFreeBytes += chunkSize;

		// we've merged one chunk header: that's memory we didn't consider free until now
		m_freeBytesCount += chunkSize;
	}

	// got a free chunk after it?
	if (nextChunk != nullptr)
	{
		nextChunk->m_previous = chunkToStartDeallocation;

		if (!nextChunk->m_isInUse)
		{
			// we're recovering all of the bytes from the chunk, plus its header
			newChunkFreeBytes += nextChunk->m_bytes + chunkSize;

			// update pointers
			nextChunk = nextChunk->m_next;
			if (nextChunk != nullptr)
			{
				nextChunk->m_previous = chunkToStartDeallocation;
			}

			chunkToStartDeallocation->m_next = nextChunk;

			// we've merged one chunk header: that's memory we didn't consider free until now
			m_freeBytesCount += chunkSize;
		}
	}

	// build a new free chunk
	unsigned char *freeChunkAddress = reinterpret_cast<unsigned char *>(chunkToStartDeallocation);
	cMemoryChunk *freeChunk = new (freeChunkAddress) cMemoryChunk(newChunkFreeBytes);

	// update pointers
	freeChunk->m_previous = previousChunk;
	freeChunk->m_next = nextChunk;

	// trashing on deallocation?
	if (static_cast<int>(m_trashing) & static_cast<int>(eTrashing::ON_DEALLOCATION))
	{
		memset(freeChunkAddress + chunkSize, static_cast<int>(eTrashingValue::ON_DEALLOCATION), freeChunk->m_bytes);
	}
}