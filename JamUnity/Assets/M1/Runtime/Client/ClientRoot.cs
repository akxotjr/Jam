using System;
using JamUnity.Core.Native;
using JamUnity.Core.Util;
using JamUnity.World.Presentation;
using JamUnity.UI.Chat;
using UnityEngine;
using JamUnity.Client.Runtime;
using JamUnity.Actor.Runtime;

namespace JamUnity.Runtime.Client
{
    public sealed class ClientRoot : MonoBehaviour
    {
        private const string InitialWorldArchetypeName = "Town01";
        private const string InitialWorldDestinationName = "town-01";

        [Serializable]
        public struct Config
        {
            public string ServerIp;
            public ushort TcpPort;
            public ushort UdpPort;
            public ulong AccountId;
            public string SharedDataManifestPath;

            public bool HeadlessMode;

            public bool LogNativeMessages;
        }

        [SerializeField] private Config config;
        [SerializeField] private Transform viewSource;
        [SerializeField] private CameraProfile cameraProfile;
        [SerializeField] private MovementProfile movementProfile;
        [SerializeField] private Camera pointerCamera;
        [SerializeField] private Transform worldPresentationRoot;
        [SerializeField] private WorldPresentationDatabase worldPresentationDB;
        [SerializeField] private ActorPresentationCatalog actorPresentationCatalog;
        [SerializeField] private ChatUI chatUI;

        private NativeManager nativeManager;
        private NetworkManager networkManager;
        private InputManager inputManager;
        private WorldManager worldManager;
        private SocialManager socialManager;
        private bool initialWorldRequested;
        private bool characterControlEnabled;
        private ulong activeMainWorldId;

        private void Awake()
        {
            if (!ValidateConfig())
            {
                Debug.LogError("ClientRoot: Invalid configuration.");
                return;
            }

            nativeManager = new NativeManager();
            networkManager = new NetworkManager();
            inputManager = new InputManager();
            worldManager = new WorldManager();
            socialManager = new SocialManager(nativeManager);

            nativeManager.BindNetworkEvent(networkManager);
            nativeManager.BindWorldEvent(worldManager);
            nativeManager.BindActorEvent(worldManager);
            nativeManager.BindSocialEvent(socialManager);
            chatUI?.Bind(socialManager);
            networkManager.StateChanged += OnNetworkStateChanged;
            worldManager.MainWorldActivated += OnMainWorldActivated;
            worldManager.LocalPlayerAvailabilityChanged += OnLocalPlayerAvailabilityChanged;
        }

        private void Start()
        {
            Camera activeCamera = pointerCamera != null ? pointerCamera : Camera.main;
            if (activeCamera != null && activeCamera.TryGetComponent(out CameraFollow cameraFollow))
                cameraFollow.SetProfile(cameraProfile);

            worldManager.Init(new WorldManager.Config
            {
                PresentationRoot = worldPresentationRoot != null ? worldPresentationRoot : transform,
                WorldPresentationDatabase = worldPresentationDB,
                ActorPresentationCatalog = actorPresentationCatalog,
            });
            inputManager.Init(new InputManager.Config
            {
                ViewSource = viewSource != null ? viewSource : transform,
                PointerCamera = activeCamera,
                CameraProfile = cameraProfile,
                MovementProfile = movementProfile,
            });
            nativeManager.Init(config);
        }

        private void Update()
        {
            inputManager?.SetInputSuppressed(chatUI != null && chatUI.HasInputFocus);

            if (nativeManager.IsInitialized && characterControlEnabled)
            {
                if (inputManager.TryBuildIntent(Time.deltaTime, out var intent))
                    nativeManager.SubmitCharacterControl(ref intent);
            }

            nativeManager.Process(Time.deltaTime);
            worldManager.Tick(nativeManager, Time.deltaTime);
        }

        private void OnDestroy()
        {
            if (networkManager != null)
                networkManager.StateChanged -= OnNetworkStateChanged;

            if (worldManager != null)
            {
                worldManager.MainWorldActivated -= OnMainWorldActivated;
                worldManager.LocalPlayerAvailabilityChanged -= OnLocalPlayerAvailabilityChanged;
            }

            chatUI?.Unbind();

            nativeManager?.Shutdown();
        }

        private void OnNetworkStateChanged(NetworkManager.StateSnapshot state)
        {
            if (state.Phase == CoreNative.eNetworkPhase.Disconnected)
            {
                initialWorldRequested = false;
                characterControlEnabled = false;
                activeMainWorldId = 0;
                inputManager?.Reset();
                return;
            }

            if (state.Phase != CoreNative.eNetworkPhase.Ready || initialWorldRequested)
                return;

            CoreNative.WorldActionCommand command = new()
            {
                kind = CoreNative.eWorldActionKind.Enter,
                enter = new CoreNative.EnterWorldRequest
                {
                    worldArchetypeKey = StableKey.MakeStableKey(InitialWorldArchetypeName),
                    selector = CoreNative.eWorldDestinationSelector.AuthoredDestination,
                    destinationName = InitialWorldDestinationName,
                    expectedMainRevision = 0,
                },
            };

            CoreNative.eResult result = nativeManager.RequestWorldAction(ref command, out CoreNative.ClientRequestSubmission submission);
            if (result != CoreNative.eResult.Ok || submission.admission != CoreNative.eClientRequestAdmission.Accepted)
            {
                Debug.LogError($"ClientRoot: initial world entry rejected. result={result}, admission={submission.admission}");
                return;
            }

            initialWorldRequested = true;
            Debug.Log($"ClientRoot: initial world entry requested. archetype={InitialWorldArchetypeName}, destination={InitialWorldDestinationName}, request={submission.receipt.requestId}");
        }

        private void OnMainWorldActivated(CoreNative.WorldRuntimeRef world)
        {
            activeMainWorldId = world.worldId;
            inputManager?.Reset();
            SetCharacterControlEnabled(world.worldId != 0 && worldManager.HasLocalPlayer(world.worldId));
        }

        private void OnLocalPlayerAvailabilityChanged(ulong worldId, bool available)
        {
            if (worldId != activeMainWorldId)
                return;
            SetCharacterControlEnabled(available);
        }

        private void SetCharacterControlEnabled(bool enabled)
        {
            if (characterControlEnabled == enabled)
                return;
            characterControlEnabled = enabled;
            if (enabled)
                inputManager.Reset();
        }

        public bool SetCameraProfile(CameraProfile profile)
        {
            return TrySetInputProfiles(profile, movementProfile);
        }

        public bool SetMovementProfile(MovementProfile profile)
        {
            return TrySetInputProfiles(cameraProfile, profile);
        }

        public bool TrySetInputProfiles(CameraProfile nextCameraProfile, MovementProfile nextMovementProfile)
        {
            if (!InputManager.IsValidProfileCombination(nextCameraProfile, nextMovementProfile))
                return false;

            if (inputManager != null && !inputManager.TrySetProfiles(nextCameraProfile, nextMovementProfile))
                return false;

            cameraProfile = nextCameraProfile;
            movementProfile = nextMovementProfile;

            Camera activeCamera = pointerCamera != null ? pointerCamera : Camera.main;
            if (activeCamera != null && activeCamera.TryGetComponent(out CameraFollow cameraFollow))
                cameraFollow.SetProfile(cameraProfile);
            return true;
        }

        private bool ValidateConfig()
        {
            if (worldPresentationDB == null)
            {
                Debug.LogError("ClientRoot: WorldPresentationDatabase is not set.");
                return false;
            }

            if (actorPresentationCatalog == null)
            {
                Debug.LogError("ClientRoot: ActorPresentationCatalog is not set.");
                return false;
            }

            if (!InputManager.IsValidProfileCombination(cameraProfile, movementProfile))
            {
                Debug.LogError($"ClientRoot: Unsupported input profile combination. camera={cameraProfile?.name}, movement={movementProfile?.name}");
                return false;
            }

            if (string.IsNullOrWhiteSpace(config.ServerIp))
            {
                Debug.LogError("ClientRoot: ServerIp is not set.");
                return false;
            }

            if (!ValidatePortNumber(config.TcpPort, config.UdpPort))
                return false;

            if (config.AccountId == 0)
            {
                Debug.LogError("ClientRoot: AccountId is 0. Please set a valid account ID.");
                return false;
            }

            if (string.IsNullOrWhiteSpace(config.SharedDataManifestPath))
            {
                Debug.LogError("ClientRoot: SharedDataManifestPath is not set.");
                return false;
            }
            if (!System.IO.Path.IsPathRooted(config.SharedDataManifestPath))
            {
                Debug.LogError("ClientRoot: SharedDataManifestPath must be an absolute path.");
                return false;
            }

            return true;
        }

        private void OnValidate()
        {
            if (cameraProfile != null
                && movementProfile != null
                && !InputManager.IsValidProfileCombination(cameraProfile, movementProfile))
                Debug.LogWarning($"ClientRoot: Unsupported input profile combination. camera={cameraProfile.name}, movement={movementProfile.name}", this);
        }

        private bool ValidatePortNumber(ushort tcpPort, ushort udpPort)
        {
            if (tcpPort == 0)
            {
                Debug.LogError("ClientRoot: TcpPort is 0. Please set a valid port number.");
                return false;
            }

            if (udpPort == 0)
            {
                Debug.LogError("ClientRoot: UdpPort is 0. Please set a valid port number.");
                return false;
            }

            if (tcpPort < 1024)
                Debug.LogWarning("ClientRoot: TcpPort is less than 1024. This may require elevated privileges.");

            if (udpPort < 1024)
                Debug.LogWarning("ClientRoot: UdpPort is less than 1024. This may require elevated privileges.");

            if (tcpPort == udpPort)
                Debug.LogWarning("ClientRoot: TcpPort and UdpPort are the same. Please set different port numbers.");

            return true;
        }
    }
}
