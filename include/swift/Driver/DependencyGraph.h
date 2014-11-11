//===--- DependencyGraph.h - Track intra-module dependencies ----*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_DRIVER_DEPENDENCYGRAPH_H
#define SWIFT_DRIVER_DEPENDENCYGRAPH_H

#include "swift/Basic/LLVM.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/PointerLikeTypeTraits.h"
#include <string>
#include <vector>

namespace llvm {
  class MemoryBuffer;
}

namespace swift {

/// The non-templated implementation of DependencyGraph.
///
/// \see DependencyGraph
class DependencyGraphImpl {
public:
  /// A mask of possible dependency kinds ORed together.
  ///
  /// Clients of DependencyGraph should have no reason to use this type.
  /// It is only used in the implementation.
  using DependencyMaskTy = uint8_t;

private:
  using DependencyPairTy = std::pair<DependencyMaskTy, const void *>;
  using ProvidesPairTy = std::pair<DependencyMaskTy, std::string>;

  /// The "outgoing" edge map. This lists all outgoing (kind, string) edges
  /// representing satisified dependencies from a particular node.
  ///
  /// For multiple outgoing edges with the same string, the kinds are combined
  /// into one field.
  ///
  /// \sa DependencyMaskTy
  llvm::DenseMap<const void *, std::vector<ProvidesPairTy>> Provides;

  /// The "incoming" edge map. Semantically this maps incoming (kind, string)
  /// edges representing dependencies to the nodes that depend on them.
  ///
  /// The representation is a map from strings to kind mask / node pairs. This
  /// is because it is unusual (though not impossible) for dependencies of
  /// different kinds to have the same strings. In the case of multiple
  /// incoming edges with the same string, the kinds are combined into the one
  /// field.
  ///
  /// \sa DependencyMaskTy
  llvm::StringMap<std::vector<DependencyPairTy>> Dependencies;

  /// The set of marked nodes.
  llvm::SmallPtrSet<const void *, 16> Marked;

  bool loadFromBuffer(const void *node, llvm::MemoryBuffer &buffer);

protected:
  bool loadFromString(const void *node, StringRef data);
  bool loadFromPath(const void *node, StringRef path);
  void markTransitive(SmallVectorImpl<const void *> &newlyMarked,
                      const void *node);

  bool isMarked(const void *node) const {
    assert(Provides.count(node) && "node is not in the graph");
    return Marked.count(node);
  }
};

/// Tracks dependencies between opaque nodes.
///
/// This is implemented in terms of separate "depends" and "provides" sets
/// that together represent edges between nodes. Abstractly, each edge is
/// labeled with a (kind, string) pair, where the "kind" distinguishes
/// different kinds of dependencies. A node's "provides" set is matched up
/// with other nodes' "depends" sets to form a traversable directed graph.
/// Information on a particular node can be updated at any time, which will
/// affect any following operations.
///
/// The graph supports a transitive "mark" operation, whose interpretation is
/// up to the client.
template <typename T>
class DependencyGraph : DependencyGraphImpl {
  using Traits = llvm::PointerLikeTypeTraits<T>;
  static_assert(Traits::NumLowBitsAvailable >= 0, "not a pointer-like type");
public:
  /// Load "depends" and "provides" data for \p node from the file at the given
  /// path.
  ///
  /// If \p node is already in the graph, outgoing edges ("provides") are
  /// cleared and replaced with the newly loaded data. Incoming edges
  /// ("depends") are not cleared; new dependencies are considered additive.
  ///
  /// If \p node has already been marked, only its outgoing edges are updated.
  bool loadFromPath(T node, StringRef path) {
    return DependencyGraphImpl::loadFromPath(Traits::getAsVoidPointer(node),
                                             path);
  }

  /// Load "depends" and "provides" data for \p node from a plain string.
  ///
  /// This is only intended for testing purposes.
  ///
  /// \sa loadFromPath
  bool loadFromString(T node, StringRef data) {
    return DependencyGraphImpl::loadFromString(Traits::getAsVoidPointer(node),
                                               data);
  }

  /// Marks \p node and all nodes that depend on \p node, and places any nodes
  /// that get transitively marked into \p newlyMarked.
  ///
  /// Nodes that have been previously marked are not included in \p newlyMarked,
  /// nor are their successors traversed, <em>even if the node's "provides" set
  /// has been updated since it was marked.</em>
  template <unsigned N>
  void markTransitive(SmallVector<T, N> &newlyMarked, T node) {
    SmallVector<const void *, N> rawMarked;
    DependencyGraphImpl::markTransitive(rawMarked,
                                        Traits::getAsVoidPointer(node));

    // FIXME: How can we avoid this copy?
    newlyMarked.reserve(newlyMarked.size() + rawMarked.size());
    for (const void *constRawNode : rawMarked) {
      void *rawNode = const_cast<void *>(constRawNode);
      newlyMarked.push_back(Traits::getFromVoidPointer(rawNode));
    }
  }

  /// Returns true if \p node has been marked (directly or transitively).
  bool isMarked(T node) const {
    return DependencyGraphImpl::isMarked(Traits::getAsVoidPointer(node));
  }
};

} // end namespace swift

#endif
