#pragma once
/*
 * DSPChain.h — Ordered chain of VST plugins with ping-pong buffer processing
 * Part of audiodsp.vsthost — Kodi Audio DSP addon
 * License: GPL-2.0-or-later
 */
#include "../plugin/IVSTPlugin.h"
#include <vector>
#include <memory>
#include <mutex>
#include <string>
#include <cstdint>

/// One slot in the processing chain.
struct ChainPlugin {
    std::unique_ptr<IVSTPlugin> plugin;
    std::string                 path;
    IVSTPlugin::PluginFormat    format;
};

/// Ordered chain of VST plugins.
///
/// Thread safety contract:
///   - addPlugin / removePlugin — may be called from any non-audio thread
///     concurrently with process(); m_pluginsMutex serialises vector mutation.
///   - movePlugin / clear — call only from the settings/stream thread with
///     audio stopped (not mutex-protected).
///   - initialize / shutdown — settings/stream thread only.
///   - process — audio render thread; acquires m_pluginsMutex for the
///     duration of the plugin iteration loop.
///   - setPluginParameter — any thread (delegates to IVSTPlugin::setParameter).
///   - getPluginParameter / getPluginParameterCount / getPluginParameterName —
///     any thread (approximately safe for display).
///   - getPlugin / getPluginCount — any thread; protected by m_pluginsMutex.
///   - serializeToJson / deserializeFromJson — settings thread.
class DSPChain {
public:
    DSPChain()  = default;
    ~DSPChain();

    // -------------------------------------------------------------------------
    // Chain management
    // Call only from non-audio thread with audio stopped or chain locked.
    // -------------------------------------------------------------------------

    /// Load a plugin from disk and append it to the chain.
    /// The plugin DLL is always loaded immediately using the chain's current
    /// sample-rate / block-size / channel settings (defaults: 44100 Hz, 1024
    /// frames, 2 channels when the chain has not yet been initialized for audio).
    /// A subsequent initialize() call will reload the plugin with the stream's
    /// actual parameters.
    /// @param path    Absolute path to the VST plugin .dll file.
    /// @param format  PluginFormat::VST2 (other formats are rejected).
    /// @return true on success.
    bool addPlugin(const std::string& path, IVSTPlugin::PluginFormat format);

    /// Remove and unload the plugin at \p index.
    bool removePlugin(int index);

    /// Move the plugin at \p from to position \p to.
    bool movePlugin(int from, int to);

    /// Unload and remove all plugins.
    void clear();

    // -------------------------------------------------------------------------
    // Initialization — called once per stream from Kodi
    // -------------------------------------------------------------------------

    /// Prepare all plugins for processing.
    /// @param sampleRate   Stream sample rate in Hz.
    /// @param maxBlockSize Maximum samples per process() call.
    /// @param numChannels  Channel count.
    /// @return true if initialization succeeded (plugin load failures are logged
    ///         but do not stop initialization of subsequent plugins).
    bool initialize(double sampleRate, int maxBlockSize, int numChannels);

    /// Shut down all plugins and release DSP resources.
    void shutdown();

    // -------------------------------------------------------------------------
    // Audio processing — real-time audio thread
    // No allocations, no I/O.  Acquires m_pluginsMutex briefly to guard
    // against concurrent addPlugin / removePlugin on the settings thread.
    // -------------------------------------------------------------------------

    /// Process one block through the entire chain via ping-pong buffers.
    /// @param in      Planar input channels (one float* per channel).
    /// @param out     Planar output channels (one float* per channel).
    /// @param samples Number of samples in this block.
    /// @return Number of samples processed (equals \p samples).
    int process(float** in, float** out, int samples);

    // -------------------------------------------------------------------------
    // Parameter access
    // -------------------------------------------------------------------------

    void        setPluginParameter(int pluginIndex, int paramIndex, float value);
    float       getPluginParameter(int pluginIndex, int paramIndex) const;
    int         getPluginParameterCount(int pluginIndex) const;
    std::string getPluginParameterName(int pluginIndex, int paramIndex) const;

    // -------------------------------------------------------------------------
    // State serialization
    // -------------------------------------------------------------------------

    /// Serialize the chain configuration and all plugin states to JSON.
    /// The returned string is suitable for writing to chain.json.
    std::string serializeToJson() const;

    /// Restore chain configuration from a JSON string produced by serializeToJson().
    /// Existing plugins are cleared first.
    bool deserializeFromJson(const std::string& json);

    // -------------------------------------------------------------------------
    // Queries
    // -------------------------------------------------------------------------

    int getPluginCount() const;

    /// Sum of getLatencySamples() for all non-bypassed plugins.
    int getTotalLatencySamples() const;

    /// Direct access to a plugin slot (nullptr if out of range).
    IVSTPlugin* getPlugin(int index);

    /// Recovery settings loaded from the "settings" block of chain.json.
    /// These are forwarded to CActiveAEDSP via ADDON_GetRecoveryParams so that
    /// the Kodi-side recovery timer honours per-chain configuration.
    int getRecoveryDelayMs()     const { return m_recoveryDelayMs; }
    int getMaxRecoveryAttempts() const { return m_maxRecoveryAttempts; }

private:
    std::vector<ChainPlugin> m_plugins;

    /// Guards m_plugins against concurrent access from the audio thread
    /// (process) and the settings/pipe thread (addPlugin, removePlugin,
    /// getPlugin, getPluginCount).
    mutable std::mutex m_pluginsMutex;

    double m_sampleRate  = 44100.0;
    int    m_blockSize   = 1024;
    int    m_numChannels = 2;
    bool   m_initialized = false;

    /// Recovery timer settings — read from chain.json "settings" block.
    /// Defaults match the hard-coded Kodi-side fallbacks in CActiveAEDSP.
    int m_recoveryDelayMs     = 30000;  ///< ms to wait before auto-reload after a crash
    int m_maxRecoveryAttempts = 3;      ///< max number of reload attempts per plugin load

    // Ping-pong scratch buffers — allocated once in initialize(), never in process().
    std::vector<std::vector<float>> m_bufA;   // channel scratch — "A" side
    std::vector<std::vector<float>> m_bufB;   // channel scratch — "B" side
    std::vector<float*>             m_ptrA;   // raw pointer array for m_bufA
    std::vector<float*>             m_ptrB;   // raw pointer array for m_bufB

    void allocatePingPong();

    // Standard base64 helpers — no external library dependency.
    static std::string           base64Encode(const std::vector<uint8_t>& data);
    static std::vector<uint8_t>  base64Decode(const std::string& str);
};
