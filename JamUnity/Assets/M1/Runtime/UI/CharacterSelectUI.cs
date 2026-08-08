using System.Collections.Generic;
using JamUnity.Core.Native;
using JamUnity.Runtime.Client;
using TMPro;
using UnityEngine;
using UnityEngine.UI;

namespace JamUnity.UI.CharacterSelect
{
    public sealed class CharacterSelectUI : MonoBehaviour
    {
        [SerializeField] private ClientRoot clientRoot;
        [SerializeField] private GameObject characterSelectOverlay;
        [SerializeField] private Transform contentRoot;
        [SerializeField] private Button characterSlotTemplate;
        [SerializeField] private TMP_Text selectedNameText;
        [SerializeField] private TMP_Text statusText;
        [SerializeField] private Button enterWorldButton;

        private readonly List<Button> slots = new();
        private IReadOnlyList<CharacterSummary> characters;
        private int selectedIndex = -1;
        private bool selectionPending;

        private void Awake()
        {
            enterWorldButton?.onClick.AddListener(EnterWorld);
            if (characterSlotTemplate != null)
                slots.Add(characterSlotTemplate);
            SetVisible(false);
            SetSelection(-1);
        }

        private void OnEnable()
        {
            if (clientRoot == null)
                return;
            clientRoot.CharacterListCompleted += OnCharacterListCompleted;
            clientRoot.CharacterSelectCompleted += OnCharacterSelectCompleted;
        }

        private void OnDisable()
        {
            if (clientRoot == null)
                return;
            clientRoot.CharacterListCompleted -= OnCharacterListCompleted;
            clientRoot.CharacterSelectCompleted -= OnCharacterSelectCompleted;
        }

        private void OnDestroy()
        {
            enterWorldButton?.onClick.RemoveListener(EnterWorld);
            ClearSlotListeners();
        }

        private void OnCharacterListCompleted(
            CoreNative.eContentResponseStatus status,
            IReadOnlyList<CharacterSummary> result)
        {
            SetVisible(true);
            if (status != CoreNative.eContentResponseStatus.Succeeded)
            {
                characters = null;
                PopulateSlots(0);
                SetSelection(-1);
                SetStatus($"Failed to load characters: {status}");
                return;
            }

            characters = result;
            PopulateSlots(characters.Count);
            SetStatus(characters.Count == 0 ? "No characters are available." : string.Empty);
            SetSelection(characters.Count > 0 ? 0 : -1);
        }

        private void OnCharacterSelectCompleted(
            CoreNative.eContentResponseStatus status,
            CharacterSummary character)
        {
            selectionPending = false;
            if (status == CoreNative.eContentResponseStatus.Succeeded)
            {
                SetVisible(false);
                return;
            }

            SetInteractable(true);
            SetStatus($"Character selection failed: {status}");
        }

        private void PopulateSlots(int count)
        {
            ClearSlotListeners();
            if (characterSlotTemplate == null || contentRoot == null)
                return;

            while (slots.Count < count)
            {
                Button slot = Instantiate(characterSlotTemplate, contentRoot);
                slots.Add(slot);
            }

            for (int i = 0; i < slots.Count; ++i)
            {
                Button slot = slots[i];
                bool active = i < count;
                slot.gameObject.SetActive(active);
                if (!active)
                    continue;

                int index = i;
                TMP_Text label = slot.GetComponentInChildren<TMP_Text>();
                if (label != null)
                    label.text = characters[i].Name;
                slot.onClick.AddListener(() => SetSelection(index));
            }
        }

        private void ClearSlotListeners()
        {
            foreach (Button slot in slots)
                slot?.onClick.RemoveAllListeners();
        }

        private void SetSelection(int index)
        {
            selectedIndex = index;
            bool valid = characters != null && index >= 0 && index < characters.Count;
            if (selectedNameText != null)
                selectedNameText.text = valid ? $"Character Name : {characters[index].Name}" : "Character Name :";
            if (enterWorldButton != null)
                enterWorldButton.interactable = valid && !selectionPending;
        }

        private void EnterWorld()
        {
            if (selectionPending || characters == null
                || selectedIndex < 0 || selectedIndex >= characters.Count || clientRoot == null)
                return;

            selectionPending = true;
            SetInteractable(false);
            SetStatus("Entering world...");
            CoreNative.eResult result = clientRoot.SelectCharacter(
                characters[selectedIndex].CharacterId,
                out CoreNative.ClientRequestSubmission submission);
            if (result == CoreNative.eResult.Ok
                && submission.admission == CoreNative.eClientRequestAdmission.Accepted)
                return;

            selectionPending = false;
            SetInteractable(true);
            SetStatus($"Character selection could not be started: {result}");
        }

        private void SetInteractable(bool value)
        {
            for (int i = 0; i < slots.Count; ++i)
                if (slots[i] != null && slots[i].gameObject.activeSelf)
                    slots[i].interactable = value;
            if (enterWorldButton != null)
                enterWorldButton.interactable = value && selectedIndex >= 0;
        }

        private void SetVisible(bool visible)
        {
            if (characterSelectOverlay != null && characterSelectOverlay.activeSelf != visible)
                characterSelectOverlay.SetActive(visible);
        }

        private void SetStatus(string value)
        {
            if (statusText != null)
                statusText.text = value;
        }
    }
}
