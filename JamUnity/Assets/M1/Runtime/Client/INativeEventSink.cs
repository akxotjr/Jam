using JamUnity.Core.Native;

namespace JamUnity.Runtime.Client
{
    public interface INativeNetworkEventSink
    {
        void OnNetworkStateChanged(CoreNative.NetworkStateChangedEvent ev);
    }

    public interface INativeWorldEventSink
    {
        void OnWorldParticipantChanged(CoreNative.WorldParticipantChangedEvent ev);
        void OnWorldRayResolved(CoreNative.WorldRayResolvedEvent ev);
    }

    public interface INativeActorEventSink
    {
        void OnActorLifecycleChanged(CoreNative.ActorLifecycleChangedEvent ev);
    }

    public interface INativeSocialEventSink
    {
        void OnSocialMessageReceived(SocialMessage message);
    }
}
