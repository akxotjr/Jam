using System.Collections;
using JamUnity.Core.Native;
using JamUnity.Runtime.Client;
using TMPro;
using UnityEngine;
using UnityEngine.UI;

namespace JamUnity.UI.Chat
{
    public sealed class ChatUI : MonoBehaviour
    {
        [Header("Input")]
        [SerializeField]
        private TMP_InputField inputField;

        [SerializeField]
        private Button sendButton;

        [Header("Message List")]
        [SerializeField]
        private RectTransform content;

        [SerializeField]
        private ScrollRect scrollRect;

        [SerializeField]
        private TMP_Text messagePrefab;

        private SocialManager socialManager;

        public bool HasInputFocus => inputField != null && inputField.isFocused;

        private void Awake()
        {
            sendButton.onClick.AddListener(SendCurrentMessage);
            inputField.onSubmit.AddListener(OnInputSubmitted);
        }

        private void OnDestroy()
        {
            Unbind();
            sendButton.onClick.RemoveListener(SendCurrentMessage);
            inputField.onSubmit.RemoveListener(OnInputSubmitted);
        }

        private void OnInputSubmitted(string text)
        {
            SendCurrentMessage();
            StartCoroutine(RefocusInputFieldNextFrame());
        }

        private void SendCurrentMessage()
        {
            string message = inputField.text.Trim();

            if (string.IsNullOrEmpty(message))
                return;

            if (socialManager == null)
                return;

            CoreNative.eResult result = socialManager.SendGlobalText(message, out CoreNative.ClientRequestSubmission submission);
            if (result != CoreNative.eResult.Ok || submission.admission != CoreNative.eClientRequestAdmission.Accepted)
            {
                Debug.LogWarning($"Chat send rejected. result={result}, admission={submission.admission}");
                return;
            }

            inputField.text = string.Empty;
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
            if (!SocialManager.TryDecodeText(message, out string text))
                return;
            string channel = message.Audience switch
            {
                CoreNative.eSocialAudience.Direct => "DM",
                CoreNative.eSocialAudience.Group => "GROUP",
                _ => "ALL",
            };
            AddMessage($"[{channel}] User {message.SenderUserId}: {text}");
        }

        private void AddMessage(string message)
        {
            TMP_Text messageText = Instantiate(messagePrefab, content);
            messageText.text = message;

            // 레이아웃 계산 후 가장 아래로 스크롤한다.
            Canvas.ForceUpdateCanvases();
            scrollRect.verticalNormalizedPosition = 0f;
        }
        
        private IEnumerator RefocusInputFieldNextFrame()
        {
            yield return null; // Wait for the next frame
            inputField.Select();
            inputField.ActivateInputField();
        }
    }
}
