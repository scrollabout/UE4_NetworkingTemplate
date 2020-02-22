// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;

public class NetworkingTemplate : ModuleRules
{
	public NetworkingTemplate(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });


        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicDependencyModuleNames.AddRange
            (

                new string[]
                {
                    "Core"              ,
                    "CoreUObject"       ,
                    "Engine"            ,
                    "HeadMountedDisplay",
                    "InputCore"         ,
                    "Slate"             ,
                    "SlateCore"         ,
                    "UMG"               ,

			     // Added
			      //"AdvancedSessions"     ,
                  //"AdvancedSteamSessions",
                  //"VRExpansionPlugin"    ,
                  //"OpenVRExpansionPlugin",

				 // Networking
					"Networking"          ,
                    "OnlineSubsystem"     ,
                    "OnlineSubsystemUtils",
                    "Sockets"             
                //"Steamworks"
                }
            );
        }

            PublicIncludePaths.AddRange
            (   
                new string[]
                {
            //     "LaserPrototype"                                         ,
            //     "LaserPrototype/Actors"                                  ,
            //     "LaserPrototype/Actors/Pawns"                            ,
            //     "LaserPrototype/Actors/Pawns/Characters"                 ,
                 "NetworkingTemplate/Framework"                               ,
                 "NetworkingTemplate/Networking"                              ,
                 "NetworkingTemplate/Networking/NetSlime"                     ,
            //     "LaserPrototype/UserInterface"                           ,
                 "NetworkingTemplate/Utilities"                                 ,
                 "NetworkingTemplate/Utilities/Bitmask"                         ,
            //     "LaserPrototype/VirtualReality"

			    // ... add public include paths required here ...
		        }
            );

            /* VR Required Modules */
            //if (Target.Platform == UnrealTargetPlatform.Win64)
            //{
            //    PrivateDependencyModuleNames.AddRange(new string[] { "SteamVR", "SteamVRController" });
            //}
            //else
            //{
            //    PrivateDependencyModuleNames.AddRange(new string[] { });
            //}
        }

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
}
