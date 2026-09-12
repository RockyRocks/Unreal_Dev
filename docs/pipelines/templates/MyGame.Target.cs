using System;
using UnrealBuildTool;

public class MyGameTarget : TargetRules
{
	public MyGameTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("MyGame");

		// PGO + ThinLTO are only valid on a monolithic game/server binary.
		if (Target.Configuration == UnrealTargetConfiguration.Shipping
			|| Target.Configuration == UnrealTargetConfiguration.Test)
		{
			LinkType = TargetLinkType.Monolithic;
			bAllowLTCG = true;
			bPreferThinLTO = true;
			bUseIncrementalLinking = false;
			bUsePDBFiles = true;
			// Keep frame pointers so Sentry can walk PGO/LTO frames when CFI is thin.
			bOmitFramePointers = false;

			string? pgoDir = Environment.GetEnvironmentVariable("UE_PGO_DIR");
			if (!string.IsNullOrEmpty(pgoDir))
			{
				PGODirectory = pgoDir;
				PGOFilenamePrefix = "MyGame-Win64-Shipping";
			}

			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				WindowsPlatform.Compiler = WindowsCompiler.Clang;
				WindowsPlatform.CompilerVersion = "18.1.8";
				WindowsPlatform.bAllowClangLinker = true;
			}

			string? thinCache = Environment.GetEnvironmentVariable("UE_THINLTO_CACHE");
			if (!string.IsNullOrEmpty(thinCache))
			{
				ThinLTOCacheDirectory = thinCache;
				ThinLTOCachePruningArguments = "cache_size=75%:cache_size_bytes=50g:prune_after=168h";
			}
		}

		// bPGOProfile / bPGOOptimize are injected by UBT CLI:
		//   -PGOProfile   or   -PGOOptimize
		// Do not hard-code them here.
	}
}
