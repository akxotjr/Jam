using JamUnity.Core.Native;
using JamUnity.Runtime.Client;
using TMPro;
using UnityEngine;
using UnityEngine.UI;

namespace JamUnity.UI.Login
{
    public sealed class LoginUI : MonoBehaviour
    {
        [SerializeField] private ClientRoot clientRoot;
        [SerializeField] private TMP_InputField loginIdInput;
        [SerializeField] private TMP_InputField passwordInput;
        [SerializeField] private Button loginButton;
        [SerializeField] private TMP_Text statusText;
        [SerializeField] private GameObject loginOverlay;

        private bool loginPending;

        private void Awake()
        {
            if (passwordInput != null)
                passwordInput.contentType = TMP_InputField.ContentType.Password;
            loginButton?.onClick.AddListener(SubmitLogin);
        }

        private void OnEnable()
        {
            if (clientRoot != null)
                clientRoot.NetworkStateChanged += OnNetworkStateChanged;
        }

        private void OnDisable()
        {
            if (clientRoot != null)
                clientRoot.NetworkStateChanged -= OnNetworkStateChanged;
        }

        private void OnDestroy()
        {
            loginButton?.onClick.RemoveListener(SubmitLogin);
        }

        private void SubmitLogin()
        {
            if (loginPending || clientRoot == null || loginIdInput == null || passwordInput == null)
                return;

            string loginId = loginIdInput.text?.Trim();
            string password = passwordInput.text;
            if (string.IsNullOrEmpty(loginId) || string.IsNullOrEmpty(password))
            {
                SetStatus("Enter both ID and password.");
                return;
            }

            loginPending = true;
            SetInteractable(false);
            SetStatus("Connecting...");
            if (clientRoot.Login(loginId, password))
                return;

            loginPending = false;
            SetInteractable(true);
            SetStatus("Login request could not be started.");
        }

        private void OnNetworkStateChanged(NetworkManager.StateSnapshot state)
        {
            switch (state.Phase)
            {
                case CoreNative.eNetworkPhase.Ready:
                    loginPending = false;
                    SetStatus(string.Empty);
                    if (loginOverlay != null)
                        loginOverlay.SetActive(false);
                    break;

                case CoreNative.eNetworkPhase.Disconnected:
                    if (!loginPending)
                        break;
                    loginPending = false;
                    SetInteractable(true);
                    SetStatus("Login failed. Check the ID, password, and server connection.");
                    passwordInput?.Select();
                    break;
            }
        }

        private void SetInteractable(bool value)
        {
            if (loginButton != null)
                loginButton.interactable = value;
            if (loginIdInput != null)
                loginIdInput.interactable = value;
            if (passwordInput != null)
                passwordInput.interactable = value;
        }

        private void SetStatus(string value)
        {
            if (statusText != null)
                statusText.text = value;
        }
    }
}
