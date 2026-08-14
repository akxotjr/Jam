using System;
using System.Collections;
using System.Collections.Generic;
using JamUnity.Core.Native;
using JamUnity.Runtime.Client;
using TMPro;
using UnityEngine;
using UnityEngine.UI;

namespace JamUnity.UI.Chat
{
    public sealed class ChatUI : MonoBehaviour
    {
        private enum ChannelKind
        {
            All,
            World,
            Direct,
        }

        private readonly struct ChatEntry
        {
            public readonly CoreNative.eSocialAudience Audience;
            public readonly ulong ScopeId;
            public readonly string DirectPeerName;
            public readonly string Text;

            public ChatEntry(CoreNative.eSocialAudience audience, ulong scopeId, string directPeerName, string text)
            {
                Audience = audience;
                ScopeId = scopeId;
				DirectPeerName = directPeerName ?? string.Empty;
                Text = text;
            }
        }

        [Header("Input")]
        [SerializeField] private TMP_InputField inputField;
        [SerializeField] private Button sendButton;

        [Header("Message List")]
        [SerializeField] private RectTransform content;
        [SerializeField] private ScrollRect scrollRect;
        [SerializeField] private TMP_Text messagePrefab;
        [SerializeField, Min(1)] private int maxHistoryEntries = 200;

        [Header("Channels")]
        [SerializeField] private Button allButton;
        [SerializeField] private Button worldButton;
        [SerializeField] private Button dmPlusButton;
        [SerializeField] private RectTransform channelContent;
        [SerializeField] private Button dmButtonPrefab;
        [SerializeField] private TMP_Text chatTitle;

        [Header("Direct Message Dialog")]
        [SerializeField] private GameObject dmRecipientDialog;
        [SerializeField] private TMP_InputField dmRecipientInput;
        [SerializeField] private Button dmStartButton;
        [SerializeField] private Button dmCancelButton;

        private readonly List<ChatEntry> history = new();
        private readonly Queue<TMP_Text> visibleMessages = new();
        private readonly Queue<TMP_Text> messagePool = new();
        private readonly Dictionary<string, Button> dmButtons = new(StringComparer.OrdinalIgnoreCase);
        private SocialManager socialManager;
        private ChannelKind activeChannel = ChannelKind.All;
        private string activeDirectCharacterName = string.Empty;
        private bool scrollToBottomPending;

        public bool HasInputFocus => (inputField != null && inputField.isFocused)
            || (dmRecipientInput != null && dmRecipientInput.isFocused);

        private void Awake()
        {
            sendButton.onClick.AddListener(SendCurrentMessage);
            inputField.onSubmit.AddListener(OnInputSubmitted);
            allButton.onClick.AddListener(SelectAllChannel);
            worldButton.onClick.AddListener(SelectWorldChannel);
            dmPlusButton.onClick.AddListener(OpenDmRecipientDialog);
            dmStartButton.onClick.AddListener(StartDirectConversation);
            dmCancelButton.onClick.AddListener(CloseDmRecipientDialog);
            dmRecipientInput.onSubmit.AddListener(OnDmRecipientSubmitted);

            CloseDmRecipientDialog();
            SelectAllChannel();
        }

        private void OnDestroy()
        {
            Unbind();
            sendButton.onClick.RemoveListener(SendCurrentMessage);
            inputField.onSubmit.RemoveListener(OnInputSubmitted);
            allButton.onClick.RemoveListener(SelectAllChannel);
            worldButton.onClick.RemoveListener(SelectWorldChannel);
            dmPlusButton.onClick.RemoveListener(OpenDmRecipientDialog);
            dmStartButton.onClick.RemoveListener(StartDirectConversation);
            dmCancelButton.onClick.RemoveListener(CloseDmRecipientDialog);
            dmRecipientInput.onSubmit.RemoveListener(OnDmRecipientSubmitted);
        }

        private void OnInputSubmitted(string text)
        {
            SendCurrentMessage();
            StartCoroutine(RefocusInputFieldNextFrame());
        }

        private void SendCurrentMessage()
        {
            string message = inputField.text.Trim();
            if (string.IsNullOrEmpty(message) || socialManager == null)
                return;

            CoreNative.eResult result;
            CoreNative.ClientRequestSubmission submission;
            if (message.StartsWith("/", StringComparison.Ordinal))
            {
                if (!TrySendCommand(message, out result, out submission))
                    return;
            }
            else
            {
                switch (activeChannel)
                {
                    case ChannelKind.World:
                        result = socialManager.SendWorldText(message, out submission);
                        break;
                    case ChannelKind.Direct:
						result = socialManager.SendDirectText(activeDirectCharacterName, message, out submission);
                        break;
                    default:
                        result = socialManager.SendGlobalText(message, out submission);
                        break;
                }
            }

            if (result != CoreNative.eResult.Ok || submission.admission != CoreNative.eClientRequestAdmission.Accepted)
            {
                Debug.LogWarning($"Chat send rejected. result={result}, admission={submission.admission}");
                return;
            }
            inputField.text = string.Empty;
        }

        private bool TrySendCommand(string message, out CoreNative.eResult result,
            out CoreNative.ClientRequestSubmission submission)
        {
            result = CoreNative.eResult.InvalidArgument;
            submission = default;
            int commandEnd = message.IndexOf(' ');
            string command = commandEnd < 0 ? message : message.Substring(0, commandEnd);
            string arguments = commandEnd < 0 ? string.Empty : message.Substring(commandEnd + 1).TrimStart();

            switch (command.ToLowerInvariant())
            {
                case "/a":
                    result = socialManager.SendGlobalText(arguments, out submission);
                    return true;
                case "/g":
                    result = socialManager.SendWorldText(arguments, out submission);
                    return true;
                case "/w":
                case "/dm":
                    int targetEnd = arguments.IndexOf(' ');
					if (targetEnd <= 0)
                    {
						Debug.LogWarning("Whisper format: /w <characterName> <message> (or /dm <characterName> <message>)");
                        return false;
                    }
					string characterName = arguments.Substring(0, targetEnd).Trim();
					result = socialManager.SendDirectText(characterName, arguments.Substring(targetEnd + 1).TrimStart(), out submission);
                    if (result == CoreNative.eResult.Ok
                        && submission.admission == CoreNative.eClientRequestAdmission.Accepted)
                    {
						EnsureDmButton(characterName);
						SelectDirectChannel(characterName);
                    }
                    return true;
                default:
                    Debug.LogWarning($"Unknown chat command: {command}");
                    return false;
            }
        }

        public void Bind(SocialManager manager)
        {
            if (ReferenceEquals(socialManager, manager))
                return;
            Unbind();
            socialManager = manager;
            if (socialManager != null)
                socialManager.MessageReceived += OnMessageReceived;
        }

        public void Unbind()
        {
            if (socialManager != null)
                socialManager.MessageReceived -= OnMessageReceived;
            socialManager = null;
        }

        private void OnMessageReceived(SocialMessage message)
        {
			if (!SocialManager.TryDecodeText(message, out string text, out string senderCharacterName))
                return;

			string directPeer = string.Empty;
            if (message.Audience == CoreNative.eSocialAudience.Direct)
            {
				if (message.RecipientKind != CoreNative.eSocialRecipientKind.CharacterName
					|| string.IsNullOrEmpty(message.RecipientName))
                    return;
				directPeer = message.RecipientName;
                EnsureDmButton(directPeer);
            }

            string channel = message.Audience switch
            {
                CoreNative.eSocialAudience.Direct => "DM",
				CoreNative.eSocialAudience.Group => socialManager.TryGetMainWorldName(out string worldName) ? worldName : "World",
                _ => "ALL",
            };
            ChatEntry entry = new(message.Audience, message.ScopeId, directPeer,
				$"[{channel}] {senderCharacterName}: {text}");
            history.Add(entry);
            TrimHistory();
            if (IsVisible(entry))
                AddVisibleMessage(entry.Text);
        }

        private void SelectAllChannel()
        {
            activeChannel = ChannelKind.All;
			activeDirectCharacterName = string.Empty;
            chatTitle.text = "# ALL";
            RebuildMessages();
        }

        private void SelectWorldChannel()
        {
            activeChannel = ChannelKind.World;
			activeDirectCharacterName = string.Empty;
			UpdateWorldTitle();
            RebuildMessages();
        }

		public void RefreshWorldChannel()
		{
			if (activeChannel != ChannelKind.World)
				return;
			UpdateWorldTitle();
			RebuildMessages();
		}

		private void UpdateWorldTitle()
		{
			chatTitle.text = socialManager != null && socialManager.TryGetMainWorldName(out string worldName)
				? $"# World {worldName}"
				: "# World";
		}

        private void SelectDirectChannel(string characterName)
        {
            activeChannel = ChannelKind.Direct;
			activeDirectCharacterName = characterName;
			chatTitle.text = $"@ {characterName}";
            RebuildMessages();
        }

        private Button EnsureDmButton(string characterName)
        {
			if (dmButtons.TryGetValue(characterName, out Button existing))
                return existing;

            Button button = Instantiate(dmButtonPrefab, channelContent);
			button.name = $"DmButton_{characterName}";
            TMP_Text label = button.GetComponentInChildren<TMP_Text>(true);
            if (label != null)
				label.text = characterName;
			button.onClick.AddListener(() => SelectDirectChannel(characterName));
			dmButtons.Add(characterName, button);
            dmPlusButton.transform.SetAsLastSibling();
            return button;
        }

        private void OpenDmRecipientDialog()
        {
            dmRecipientInput.text = string.Empty;
            dmRecipientDialog.SetActive(true);
            dmRecipientDialog.transform.SetAsLastSibling();
            dmRecipientInput.Select();
            dmRecipientInput.ActivateInputField();
        }

        private void CloseDmRecipientDialog()
        {
            dmRecipientDialog.SetActive(false);
        }

        private void OnDmRecipientSubmitted(string value)
        {
            StartDirectConversation();
        }

        private void StartDirectConversation()
        {
			string characterName = dmRecipientInput.text.Trim();
			if (string.IsNullOrEmpty(characterName))
            {
				Debug.LogWarning("Direct message recipient must be a character name.");
                return;
            }

			EnsureDmButton(characterName);
            CloseDmRecipientDialog();
			SelectDirectChannel(characterName);
            inputField.Select();
            inputField.ActivateInputField();
        }

        private bool IsVisible(ChatEntry entry)
        {
            switch (activeChannel)
            {
                case ChannelKind.All:
                    return true;
                case ChannelKind.World:
                    return entry.Audience == CoreNative.eSocialAudience.Group
                        && socialManager != null
                        && socialManager.TryGetMainWorldId(out ulong worldId)
                        && entry.ScopeId == worldId;
                case ChannelKind.Direct:
                    return entry.Audience == CoreNative.eSocialAudience.Direct
						&& string.Equals(entry.DirectPeerName, activeDirectCharacterName, StringComparison.OrdinalIgnoreCase);
                default:
                    return false;
            }
        }

        private void RebuildMessages()
        {
            while (visibleMessages.Count > 0)
                ReturnMessageToPool(visibleMessages.Dequeue());
            foreach (ChatEntry entry in history)
            {
                if (IsVisible(entry))
                    AddVisibleMessage(entry.Text, false);
            }
            ScrollToBottom();
        }

        private void AddVisibleMessage(string message, bool scroll = true)
        {
            TMP_Text messageText = RentMessage();
            messageText.text = message;
            visibleMessages.Enqueue(messageText);
            int historyLimit = Mathf.Max(1, maxHistoryEntries);
            while (visibleMessages.Count > historyLimit)
                ReturnMessageToPool(visibleMessages.Dequeue());
            if (scroll)
                ScrollToBottom();
        }

        private TMP_Text RentMessage()
        {
            TMP_Text message = messagePool.Count > 0
                ? messagePool.Dequeue()
                : Instantiate(messagePrefab, content);
            message.gameObject.SetActive(true);
            message.transform.SetAsLastSibling();
            return message;
        }

        private void ReturnMessageToPool(TMP_Text message)
        {
            if (message == null)
                return;
            message.text = string.Empty;
            message.gameObject.SetActive(false);
            messagePool.Enqueue(message);
        }

        private void ScrollToBottom()
        {
            scrollToBottomPending = true;
        }

        private void LateUpdate()
        {
            if (!scrollToBottomPending)
                return;

            scrollToBottomPending = false;
            Canvas.ForceUpdateCanvases();
            scrollRect.verticalNormalizedPosition = 0f;
        }

        private void TrimHistory()
        {
            int overflow = history.Count - Mathf.Max(1, maxHistoryEntries);
            if (overflow > 0)
                history.RemoveRange(0, overflow);
        }

        private IEnumerator RefocusInputFieldNextFrame()
        {
            yield return null;
            inputField.Select();
            inputField.ActivateInputField();
        }
    }
}
