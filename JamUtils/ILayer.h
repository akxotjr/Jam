#pragma once

namespace jam
{
    class ILayer
    {
    public:
        virtual ~ILayer() = default;

        //NODISCARD virtual TypeID GetPluginTypeID() const { return 0; } // runtime reflection -> 대충 return 0 처리하고 개발
        //virtual void             OnEvent(Event* in_event) const {} // 애플리케이션이랑 통신할 수 있는 함수 (일단 빈 함수로 처리)

        // ---------- Layer loop ----------

        // 따로 매개변수 초기화가 필요하면 void Init( ... ) 형태로 추가할 것

        virtual void OnAttach() {}   // 어플리케이션에 붙일 때 호출
        virtual void OnDetach() {}   // 어플리케이션에서 떨어질 때 혹은 어플리케이션이 종료할 때 호출

        virtual void Init() {}
        virtual void Shutdown() {}

        virtual void OnUpdate() {} // 매 프레임 호출
        virtual void OnLateUpdate() {} // OnUpdate 이후에 호출

        virtual void OnBeginRender() {} // 렌더링 전에 호출
        virtual void OnRender() {} // 렌더링
        virtual void OnRenderUI() {} // UI 렌더링
        virtual void OnEndRender() {} // 렌더링 끝나고 호출

    protected:
        //EventBus m_eventBus; // 이벤트 버스, 애플리케이션과 통신할 때 사용 일단 주석처리
    };

}
