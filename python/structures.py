"""
Seven data structures for dictionary lookup.

1. HashMap (Python dict)
2. Sorted Array + Binary Search
3. Trie (Prefix Tree)
4. Ternary Search Tree (TST)
5. DAWG (Directed Acyclic Word Graph)
6. Bloom Filter
7. Cuckoo Filter

Every structure implements the same interface:
  - build(words)          build the structure from a list of words
  - lookup(word)          True or False, whether the word is present
  - prefix_search(prefix) words starting with the prefix, where supported
  - memory_usage()        estimated memory in bytes

Authors: Nikola Vukotic, Slavimir Stosovic
"""

import sys
import math
import array
import bisect
import hashlib
import struct
from typing import List, Optional


# ============================================================
# 1. HashMap
# ============================================================

class HashMapStructure:
    """Python dict, used as a hash table for exact lookup."""

    name = "HashMap"
    supports_prefix = False
    probabilistic = False

    def __init__(self):
        self._data = None

    def build(self, words: List[str]):
        self._data = {w: True for w in words}

    def lookup(self, word: str) -> bool:
        return word in self._data

    def prefix_search(self, prefix: str) -> List[str]:
        raise NotImplementedError("HashMap does not support prefix search")

    def memory_usage(self) -> int:
        return sys.getsizeof(self._data) + sum(
            sys.getsizeof(k) for k in self._data
        )


# ============================================================
# 2. Sorted Array + Binary Search
# ============================================================

class SortedArrayStructure:
    """Sorted array searched by bisection."""

    name = "Sorted Array + Binary Search"
    supports_prefix = False
    probabilistic = False

    def __init__(self):
        self._data = None

    def build(self, words: List[str]):
        self._data = sorted(words)

    def lookup(self, word: str) -> bool:
        i = bisect.bisect_left(self._data, word)
        return i < len(self._data) and self._data[i] == word

    def prefix_search(self, prefix: str) -> List[str]:
        raise NotImplementedError("Sorted array has no efficient prefix search")

    def memory_usage(self) -> int:
        return sys.getsizeof(self._data) + sum(
            sys.getsizeof(w) for w in self._data
        )


# ============================================================
# 3. Trie (Prefix Tree)
# ============================================================

class TrieNode:
    __slots__ = ('children', 'is_end')

    def __init__(self):
        self.children = {}
        self.is_end = False


class TrieStructure:
    """Plain prefix tree."""

    name = "Trie"
    supports_prefix = True
    probabilistic = False

    def __init__(self):
        self._root = None
        self._node_count = 0

    def build(self, words: List[str]):
        self._root = TrieNode()
        self._node_count = 1
        for word in words:
            node = self._root
            for ch in word:
                if ch not in node.children:
                    node.children[ch] = TrieNode()
                    self._node_count += 1
                node = node.children[ch]
            node.is_end = True

    def lookup(self, word: str) -> bool:
        node = self._root
        for ch in word:
            if ch not in node.children:
                return False
            node = node.children[ch]
        return node.is_end

    def prefix_search(self, prefix: str, max_results: int = 100) -> List[str]:
        node = self._root
        for ch in prefix:
            if ch not in node.children:
                return []
            node = node.children[ch]

        results = []
        self._collect(node, list(prefix), results, max_results)
        return results

    def _collect(self, node, path, results, max_results):
        if len(results) >= max_results:
            return
        if node.is_end:
            results.append(''.join(path))
        for ch, child in node.children.items():
            path.append(ch)
            self._collect(child, path, results, max_results)
            path.pop()

    def memory_usage(self) -> int:
        # Per node: __slots__ overhead plus the children dict
        # Estimated at about 200 bytes per node, dict and references included
        return self._node_count * 200


# ============================================================
# 4. Ternary Search Tree (TST)
# ============================================================

class TSTNode:
    __slots__ = ('char', 'left', 'mid', 'right', 'is_end')

    def __init__(self, char):
        self.char = char
        self.left = None
        self.mid = None
        self.right = None
        self.is_end = False


class TernarySearchTree:
    """Ternary search tree, a compromise between a trie and a BST."""

    name = "Ternary Search Tree"
    supports_prefix = True
    probabilistic = False

    def __init__(self):
        self._root = None
        self._node_count = 0

    def build(self, words: List[str]):
        # Shuffled insertion order gives a better balanced tree
        import random
        shuffled = list(words)
        random.Random(42).shuffle(shuffled)
        for word in shuffled:
            if word:
                self._root = self._insert(self._root, word, 0)

    def _insert(self, node, word, idx):
        ch = word[idx]
        if node is None:
            node = TSTNode(ch)
            self._node_count += 1

        if ch < node.char:
            node.left = self._insert(node.left, word, idx)
        elif ch > node.char:
            node.right = self._insert(node.right, word, idx)
        else:
            if idx + 1 < len(word):
                node.mid = self._insert(node.mid, word, idx + 1)
            else:
                node.is_end = True
        return node

    def lookup(self, word: str) -> bool:
        if not word:
            return False
        node = self._search(self._root, word, 0)
        return node is not None and node.is_end

    def _search(self, node, word, idx):
        if node is None:
            return None
        ch = word[idx]
        if ch < node.char:
            return self._search(node.left, word, idx)
        elif ch > node.char:
            return self._search(node.right, word, idx)
        else:
            if idx + 1 == len(word):
                return node
            return self._search(node.mid, word, idx + 1)

    def prefix_search(self, prefix: str, max_results: int = 100) -> List[str]:
        if not prefix:
            return []
        node = self._search(self._root, prefix, 0)
        if node is None:
            return []
        results = []
        if node.is_end:
            results.append(prefix)
        self._collect(node.mid, list(prefix), results, max_results)
        return results

    def _collect(self, node, path, results, max_results):
        if node is None or len(results) >= max_results:
            return
        self._collect(node.left, path, results, max_results)
        path.append(node.char)
        if node.is_end:
            results.append(''.join(path))
        self._collect(node.mid, path, results, max_results)
        path.pop()
        self._collect(node.right, path, results, max_results)

    def memory_usage(self) -> int:
        # Per node: five fields of about 8 bytes plus about 64 bytes of overhead
        return self._node_count * 104


# ============================================================
# 5. DAWG (Directed Acyclic Word Graph)
# ============================================================

class DAWGNode:
    """DAWG node, hashed so that identical suffixes can be shared."""

    _next_id = 0

    def __init__(self):
        self.id = DAWGNode._next_id
        DAWGNode._next_id += 1
        self.children = {}
        self.is_end = False
        self._hash = None

    def __hash__(self):
        if self._hash is None:
            self._hash = hash((
                self.is_end,
                tuple(sorted(
                    (ch, child.id) for ch, child in self.children.items()
                ))
            ))
        return self._hash

    def __eq__(self, other):
        return (self.is_end == other.is_end and
                len(self.children) == len(other.children) and
                all(ch in other.children and
                    self.children[ch].id == other.children[ch].id
                    for ch in self.children))


class DAWGStructure:
    """
    DAWG, sharing prefixes as a trie does and suffixes in addition.
    Implementacija: build Trie pa kompresuj sufiks deljenje.
    """

    name = "DAWG"
    supports_prefix = True
    probabilistic = False

    def __init__(self):
        self._root = None
        self._node_count = 0

    def build(self, words: List[str]):
        DAWGNode._next_id = 0

        # Step 1: build a plain trie
        root = DAWGNode()
        for word in sorted(words):  # the construction requires sorted input
            node = root
            for ch in word:
                if ch not in node.children:
                    node.children[ch] = DAWGNode()
                node = node.children[ch]
            node.is_end = True

        # Step 2: compress bottom up by sharing identical suffixes
        self._registry = {}
        self._root = self._compress(root)
        self._node_count = len(self._registry) + 1

    def _compress(self, node):
        """Merge shared suffixes recursively."""
        for ch in list(node.children):
            node.children[ch] = self._compress(node.children[ch])

        # Reuse an existing node if an identical one was already built
        node._hash = None  # reset hash
        key = (node.is_end, tuple(sorted(
            (ch, id(child)) for ch, child in node.children.items()
        )))
        if key in self._registry:
            return self._registry[key]

        self._registry[key] = node
        return node

    def lookup(self, word: str) -> bool:
        node = self._root
        for ch in word:
            if ch not in node.children:
                return False
            node = node.children[ch]
        return node.is_end

    def prefix_search(self, prefix: str, max_results: int = 100) -> List[str]:
        node = self._root
        for ch in prefix:
            if ch not in node.children:
                return []
            node = node.children[ch]

        results = []
        self._collect(node, list(prefix), results, max_results)
        return results

    def _collect(self, node, path, results, max_results):
        if len(results) >= max_results:
            return
        if node.is_end:
            results.append(''.join(path))
        for ch, child in node.children.items():
            path.append(ch)
            self._collect(child, path, results, max_results)
            path.pop()

    def memory_usage(self) -> int:
        return self._node_count * 200


# ============================================================
# 6. Bloom Filter
# ============================================================

class BloomFilter:
    """
    Bloom filter, a probabilistic membership structure.
    It can report false positives but never false negatives.
    """

    name = "Bloom Filter"
    supports_prefix = False
    probabilistic = True

    def __init__(self, expected_items: int = 0, fp_rate: float = 0.01):
        self._fp_rate = fp_rate
        self._expected = expected_items
        self._bits = None
        self._size = 0
        self._num_hashes = 0

    def build(self, words: List[str]):
        n = len(words)
        # Optimal bit array size: m = -n*ln(p) / (ln2)^2
        self._size = max(64, int(-n * math.log(self._fp_rate) / (math.log(2) ** 2)))
        # Optimal number of hash functions: k = (m/n) * ln2
        self._num_hashes = max(1, int((self._size / n) * math.log(2)))

        self._bits = bytearray(self._size // 8 + 1)

        for word in words:
            for i in range(self._num_hashes):
                pos = self._hash(word, i) % self._size
                self._bits[pos // 8] |= (1 << (pos % 8))

    def _hash(self, word: str, seed: int) -> int:
        h = hashlib.md5(f"{seed}:{word}".encode()).digest()
        return struct.unpack('<I', h[:4])[0]

    def lookup(self, word: str) -> bool:
        for i in range(self._num_hashes):
            pos = self._hash(word, i) % self._size
            if not (self._bits[pos // 8] & (1 << (pos % 8))):
                return False
        return True  # possibly present, a false positive is allowed here

    def prefix_search(self, prefix: str) -> List[str]:
        raise NotImplementedError("Bloom filter does not support prefix search")

    def memory_usage(self) -> int:
        return len(self._bits) + sys.getsizeof(self._bits)

    def measured_fp_rate(self, words: List[str], test_words: List[str]) -> float:
        """Measure the observed false positive rate on words absent from the dictionary."""
        fp = sum(1 for w in test_words if self.lookup(w) and w not in set(words))
        return fp / len(test_words) if test_words else 0


# ============================================================
# 7. Cuckoo Filter
# ============================================================

class CuckooFilter:
    """
    Cuckoo filter, a later alternative to the Bloom filter.
    It supports deletion and is claimed to use less space below a 3 percent
    false positive rate.
    """

    name = "Cuckoo Filter"
    supports_prefix = False
    probabilistic = True

    MAX_KICKS = 500
    BUCKET_SIZE = 4

    def __init__(self, capacity: int = 0, fingerprint_bits: int = 8,
                 expected_items: int = 0):
        self._fp_bits = fingerprint_bits
        self._capacity = capacity
        self._buckets = None
        self._num_buckets = 0

    def build(self, words: List[str]):
        n = len(words)
        # Bucket count: n / bucket_size * 1.05, leaving room for the load factor
        self._num_buckets = max(64, int(n / self.BUCKET_SIZE * 1.05))
        # Round up to a power of two
        self._num_buckets = 1 << (self._num_buckets - 1).bit_length()
        self._buckets = [[] for _ in range(self._num_buckets)]

        for word in words:
            if not self._insert(word):
                # Rare: the filter is full, so grow it and rebuild
                self._num_buckets *= 2
                old_buckets = self._buckets
                self._buckets = [[] for _ in range(self._num_buckets)]
                # Re-insert sve
                for bucket in old_buckets:
                    for fp, _ in bucket:
                        # Simplified re-insert
                        pass
                self._insert(word)

    def _fingerprint(self, word: str) -> int:
        h = hashlib.md5(word.encode()).digest()
        return (struct.unpack('<I', h[:4])[0] % ((1 << self._fp_bits) - 1)) + 1

    def _hash1(self, word: str) -> int:
        h = hashlib.sha256(word.encode()).digest()
        return struct.unpack('<I', h[:4])[0] % self._num_buckets

    def _hash2(self, i1: int, fp: int) -> int:
        h = hashlib.md5(str(fp).encode()).digest()
        delta = struct.unpack('<I', h[:4])[0] % self._num_buckets
        return (i1 ^ delta) % self._num_buckets

    def _insert(self, word: str) -> bool:
        fp = self._fingerprint(word)
        i1 = self._hash1(word)
        i2 = self._hash2(i1, fp)

        if len(self._buckets[i1]) < self.BUCKET_SIZE:
            self._buckets[i1].append((fp, i1))
            return True
        if len(self._buckets[i2]) < self.BUCKET_SIZE:
            self._buckets[i2].append((fp, i2))
            return True

        # Cuckoo displacement
        import random
        rng = random.Random(42)
        idx = rng.choice([i1, i2])
        for _ in range(self.MAX_KICKS):
            # Evict a random entry from the bucket
            victim_idx = rng.randrange(len(self._buckets[idx]))
            fp, self._buckets[idx][victim_idx] = self._buckets[idx][victim_idx][0], (fp, idx)
            idx = self._hash2(idx, fp)
            if len(self._buckets[idx]) < self.BUCKET_SIZE:
                self._buckets[idx].append((fp, idx))
                return True

        return False  # Filter pun

    def lookup(self, word: str) -> bool:
        fp = self._fingerprint(word)
        i1 = self._hash1(word)
        i2 = self._hash2(i1, fp)

        for stored_fp, _ in self._buckets[i1]:
            if stored_fp == fp:
                return True
        for stored_fp, _ in self._buckets[i2]:
            if stored_fp == fp:
                return True
        return False

    def prefix_search(self, prefix: str) -> List[str]:
        raise NotImplementedError("Cuckoo filter does not support prefix search")

    def memory_usage(self) -> int:
        total = sys.getsizeof(self._buckets)
        for bucket in self._buckets:
            total += sys.getsizeof(bucket)
            for item in bucket:
                total += sys.getsizeof(item)
        return total


# ============================================================
# REGISTRY of all structures
# ============================================================

ALL_STRUCTURES = [
    HashMapStructure,
    SortedArrayStructure,
    TrieStructure,
    TernarySearchTree,
    DAWGStructure,
    BloomFilter,
    CuckooFilter,
]


def get_structure(name: str):
    """Return the structure class registered under this name."""
    for cls in ALL_STRUCTURES:
        if cls.name.lower() == name.lower():
            return cls
    raise ValueError(f"Nepoznata struktura: {name}")
