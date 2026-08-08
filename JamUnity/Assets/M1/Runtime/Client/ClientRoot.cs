using System;
using JamUnity.Core.Native;
using JamUnity.Core.Util;
using JamUnity.World.Presentation;
using JamUnity.UI.Chat;
using UnityEngine;
using JamUnity.Client.Runtime;
using JamUnity.Actor.Runtime;
using System.Collections.Generic;

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
            public string SharedDataManifestPath;

            public bool HeadlessMode;

            public bool LogNativeMessages;
        }

        [SerializeField] private Config config;
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
        private CharacterContent characterContent;
        private CameraFollow cameraFollow;
        private bool characterListRequested;
        private bool initialWorldRequested;
        private bool characterControlEnabled;
        private ulong activeMainWorldId;

		public event Action<NetworkManager.StateSnapshot> NetworkStateChanged;
		public event Action<CoreNative.eContentResponseStatus, IReadOnlyList<CharacterSummary>> CharacterListCompleted;
		public event Action<CoreNative.eContentResponseStatus, CharacterSummary> CharacterSelectCompleted;

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
            socialManager = new SocialManager(nativeManager, worldPresentationDB);
            characterContent = new CharacterContent(nativeManager);
			characterContent.CharacterListCompleted += OnCharacterListCompleted;
			characterContent.CharacterSelectCompleted += OnCharacterSelectCompleted;

            nativeManager.BindNetworkEvent(networkManager);
            nativeManager.BindWorldEvent(worldManager);
            nativeManager.BindActorEvent(worldManager);
            nativeManager.BindSocialEvent(socialManager);
            chatUI?.Bind(socialManager);
            SetChatVisible(false);
            networkManager.StateChanged += OnNetworkStateChanged;
            worldManager.MainWorldActivated += OnMainWorldActivated;
            worldManager.LocalPlayerAvailabilityChanged += OnLocalPlayerAvailabilityChanged;
        }

        private void Start()
        {
            Camera activeCamera = pointerCamera != null ? pointerCamera : Camera.main;
            if (activeCamera != null && activeCamera.TryGetComponent(out cameraFollow))
                cameraFollow.SetProfile(cameraProfile);

            worldManager.Init(new WorldManager.Config
            {
                PresentationRoot = worldPresentationRoot != null ? worldPresentationRoot : transform,
                WorldPresentationDatabase = worldPresentationDB,
                ActorPresentationCatalog = actorPresentationCatalog,
                LocalActorSmoothSpeed = cameraProfile.SmoothSpeed,
            });
            inputManager.Init(new InputManager.Config
            {
                PointerCamera = activeCamera,
                CameraProfile = cameraProfile,
                MovementProfile = movementProfile,
            });
        }

        private void Update()
        {
            inputManager?.SetInputSuppressed(chatUI != null && chatUI.HasInputFocus);

            if (nativeManager.IsInitialized && characterControlEnabled)
            {
                if (inputManager != null && inputManager.TryBuildIntent(Time.deltaTime, out var intent))
                    nativeManager.SubmitCharacterControl(ref intent);
            }

            if (cameraFollow != null && inputManager != null && cameraProfile.Kind == CameraProfileKind.ThirdPerson)
                cameraFollow.SetOrbit(inputManager.OrbitYaw, inputManager.OrbitPitch);

            nativeManager.Process(Time.deltaTime);
            worldManager.Tick(nativeManager, Time.deltaTime);
        }

        private void OnApplicationFocus(bool hasFocus)
        {
            if (!hasFocus)
                inputManager?.ReleasePointerLock();
        }

        private void OnDestroy()
        {
            inputManager?.ReleasePointerLock();

            if (networkManager != null)
                networkManager.StateChanged -= OnNetworkStateChanged;

            if (worldManager != null)
            {
                worldManager.MainWorldActivated -= OnMainWorldActivated;
                worldManager.LocalPlayerAvailabilityChanged -= OnLocalPlayerAvailabilityChanged;
            }

            chatUI?.Unbind();

			if (characterContent != null)
			{
				characterContent.CharacterListCompleted -= OnCharacterListCompleted;
				characterContent.CharacterSelectCompleted -= OnCharacterSelectCompleted;
				characterContent.Dispose();
			}

            nativeManager?.Shutdown();
        }

        private void OnNetworkStateChanged(NetworkManager.StateSnapshot state)
        {
			NetworkStateChanged?.Invoke(state);
            if (state.Phase == CoreNative.eNetworkPhase.Disconnected)
            {
				characterListRequested = false;
                initialWorldRequested = false;
                characterControlEnabled = false;
                activeMainWorldId = 0;
                SetChatVisible(false);
                inputManager?.ReleasePointerLock();
                inputManager?.Reset();
				nativeManager?.Shutdown();
                return;
            }

			if (state.Phase != CoreNative.eNetworkPhase.Ready || characterListRequested
				|| state.BootstrapKind != CoreNative.eBootstrapKind.Fresh)
				return;

			CoreNative.eResult result = characterContent.RequestCharacterList(out CoreNative.ClientRequestSubmission submission);
			if (result != CoreNative.eResult.Ok || submission.admission != CoreNative.eClientRequestAdmission.Accepted)
			{
				Debug.LogError($"ClientRoot: character list request rejected. result={result}, admission={submission.admission}");
				return;
			}

			characterListRequested = true;
			Debug.Log($"ClientRoot: character list requested. request={submission.receipt.requestId}");
		}

		private void OnCharacterListCompleted(
			CoreNative.eContentResponseStatus status,
			IReadOnlyList<CharacterSummary> characters)
		{
			CharacterListCompleted?.Invoke(status, characters);
		}

		private void OnCharacterSelectCompleted(
			CoreNative.eContentResponseStatus status,
			CharacterSummary character)
		{
			if (status == CoreNative.eContentResponseStatus.Succeeded && !RequestInitialWorld())
				status = CoreNative.eContentResponseStatus.Unavailable;
			CharacterSelectCompleted?.Invoke(status, character);
		}

		private bool RequestInitialWorld()
		{
			if (initialWorldRequested)
				return true;

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
				return false;
            }

            initialWorldRequested = true;
            Debug.Log($"ClientRoot: initial world entry requested. archetype={InitialWorldArchetypeName}, destination={InitialWorldDestinationName}, request={submission.receipt.requestId}");
			return true;
        }

        private void OnMainWorldActivated(CoreNative.WorldRuntimeRef world)
        {
            activeMainWorldId = world.worldId;
            SetChatVisible(world.worldId != 0);
			chatUI?.RefreshWorldChannel();
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

        private void SetChatVisible(bool visible)
        {
            if (chatUI != null && chatUI.gameObject.activeSelf != visible)
                chatUI.gameObject.SetActive(visible);
        }

        public bool SetCameraProfile(CameraProfile profile)
        {
            return TrySetInputProfiles(profile, movementProfile);
        }

		public bool Login(string loginId, string password)
		{
			if (nativeManager == null || nativeManager.IsInitialized || string.IsNullOrWhiteSpace(loginId) || string.IsNullOrEmpty(password))
				return false;

			return nativeManager.Login(config, loginId, password);
		}

		public CoreNative.eResult SelectCharacter(
			ulong characterId,
			out CoreNative.ClientRequestSubmission submission)
		{
			if (characterContent == null || initialWorldRequested)
			{
				submission = default;
				return CoreNative.eResult.InvalidArgument;
			}
			return characterContent.SelectCharacter(characterId, out submission);
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
            worldManager?.ConfigureLocalActorSmoothing(cameraProfile.SmoothSpeed);

            Camera activeCamera = pointerCamera != null ? pointerCamera : Camera.main;
            if (activeCamera != null && activeCamera.TryGetComponent(out cameraFollow))
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
            if (cameraProfile != null && movementProfile != null && !InputManager.IsValidProfileCombination(cameraProfile, movementProfile))
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
