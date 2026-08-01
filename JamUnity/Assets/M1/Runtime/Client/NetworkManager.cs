using System;
using JamUnity.Core.Native;
using UnityEngine;

namespace JamUnity.Runtime.Client
{
    public sealed class NetworkManager : INativeNetworkEventSink
    {
        public readonly struct StateSnapshot
        {
            public readonly CoreNative.eNetworkPhase Phase;
            public readonly ulong AccountId;
            public readonly ulong UserId;

            public StateSnapshot(CoreNative.eNetworkPhase phase, ulong accountId, ulong userId)
            {
                Phase = phase;
                AccountId = accountId;
                UserId = userId;
            }

            public bool IsConnected =>
                Phase != CoreNative.eNetworkPhase.Disconnected;
        }

        private StateSnapshot state = new(CoreNative.eNetworkPhase.Disconnected, 0, 0);

        public CoreNative.eNetworkPhase Phase => state.Phase;
        public ulong AccountId => state.AccountId;
        public ulong UserId => state.UserId;
        public bool IsConnected => state.IsConnected;
        public StateSnapshot Snapshot => state;

        public event Action<StateSnapshot> StateChanged;

        public void OnNetworkStateChanged(CoreNative.NetworkStateChangedEvent ev)
        {
            StateSnapshot next = new(ev.state.phase, ev.accountId, ev.userId);
            if (state.Phase == next.Phase
                && state.AccountId == next.AccountId
                && state.UserId == next.UserId)
            {
                return;
            }

            state = next;
            
            // temp
            if (state.Phase == CoreNative.eNetworkPhase.Ready)
            {
                Debug.Log($"NetworkManager: Connected. AccountId={state.AccountId}, UserId={state.UserId}");
            }
            
            StateChanged?.Invoke(state);
        }
    }
}
