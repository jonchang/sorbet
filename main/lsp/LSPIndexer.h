#ifndef RUBY_TYPER_LSP_LSPINDEXER_H
#define RUBY_TYPER_LSP_LSPINDEXER_H

#include "common/concurrency/WorkerPool.h"
#include "common/kvstore/KeyValueStore.h"
#include "core/NameHash.h"
#include "core/core.h"
#include "main/lsp/LSPMessage.h"

namespace sorbet::realmain::lsp {

class SorbetWorkspaceEditParams;
class LSPConfiguration;
/**
 * The indexer keeps a GlobalState object up-to-date with the latest edits, maintains a set of FileHashes for
 * every file in the workspace, and uses said hashes to decide if edits can be incrementally typechecked on the
 * fast path or necessitate retypechecking the world on the slow path.
 *
 * The actual work of typechecking is undertaken by LSPTypechecker.
 */
class LSPIndexer final {
    bool initialized = false;
    /** Encapsulates the active configuration for the language server. */
    const std::shared_ptr<const LSPConfiguration> config;
    /** Global state that we keep up-to-date with file edits. We do _not_ typecheck using this global state! We clone
     * this global state every time we need to perform a slow path typechecking operation. */
    std::unique_ptr<core::GlobalState> initialGS;
    /** Contains a copy of the last edit committed on the slow path. Used in slow path cancelation logic. */
    LSPFileUpdates pendingTypecheckUpdates;
    /** Contains a clone of the latency timer for the pending typecheck operation. Is used to ensure that we correctly
     * track the latency of canceled & rescheduled typechecking operations. */
    std::unique_ptr<Timer> pendingTypecheckLatencyTimer;
    /** Contains files evicted by `pendingTypecheckUpdates`. Used to make fast path decisions in the immediate past. */
    UnorderedMap<int, std::shared_ptr<core::File>> evictedFiles;
    std::unique_ptr<KeyValueStore> kvstore; // always null for now.
    /** A WorkerPool with 0 workers. */
    std::unique_ptr<WorkerPool> emptyWorkers;

    void computeFileHashes(const std::vector<std::shared_ptr<core::File>> &files, WorkerPool &workers) const;

public:
    LSPIndexer(std::shared_ptr<const LSPConfiguration> config, std::unique_ptr<core::GlobalState> initialGS);
    ~LSPIndexer();

    /** Determines if the given edit can take the fast path relative to the most recently committed edit. If
     * `containsPendingTypecheckUpdates` is `true`, it will make the determination in the immediate past using
     * `evictedFiles`. */
    FastPathDecision canTakeFastPath(const LSPFileUpdates &edit, bool containsPendingTypecheckUpdates = false) const;
    FastPathDecision canTakeFastPath(const std::vector<std::shared_ptr<core::File>> &changedFiles,
                                     bool containsPendingTypecheckUpdates = false) const;

    /**
     * Computes state hashes for the given set of files. Is a no-op if the provided files all have hashes.
     */
    void computeFileHashes(const std::vector<std::shared_ptr<core::File>> &files) const;

    /** Initializes the indexer by indexing and hashing all files in the workspace. Mutates the LSPFileUpdates so it can
     * be passed to the typechecker to initialize it. */
    void initialize(LSPFileUpdates &updates, WorkerPool &workers);

    /**
     * Commits the given edit to `initialGS`, and returns a canonical LSPFileUpdates object containing indexed trees
     * and file hashes. Also handles canceling the running slow path.
     */
    LSPFileUpdates commitEdit(std::unique_ptr<Timer> &latencyTimer, SorbetWorkspaceEditParams &edit,
                              FastPathDecision cachedFastPathDecision);
};

} // namespace sorbet::realmain::lsp

#endif // RUBY_TYPER_LSP_LSPINDEXER_H