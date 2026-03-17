#pragma once

#ifdef TOOLS_ENABLED

// Deploys the built-in spatial audio GDScript files to the project's
// addons folder if they don't already exist, and auto-enables the plugin.
// Files are embedded as string constants at compile time.

class SpatialAudioDeployer {
public:
	static void deploy_if_needed();
};

#endif // TOOLS_ENABLED
