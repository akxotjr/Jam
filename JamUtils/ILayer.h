#pragma once

namespace jam
{
    class ILayer
    {
    public:
        virtual ~ILayer() = default;

        //NODISCARD virtual TypeID GetPluginTypeID() const { return 0; } // runtime reflection -> ���� return 0 ó���ϰ� ����
        //virtual void             OnEvent(Event* in_event) const {} // ���ø����̼��̶� ����� �� �ִ� �Լ� (�ϴ� �� �Լ��� ó��)

        // ---------- Layer loop ----------

        // ���� �Ű����� �ʱ�ȭ�� �ʿ��ϸ� void Init( ... ) ���·� �߰��� ��

        virtual void OnAttach() {}   // ���ø����̼ǿ� ���� �� ȣ��
        virtual void OnDetach() {}   // ���ø����̼ǿ��� ������ �� Ȥ�� ���ø����̼��� ������ �� ȣ��

        virtual void Init() {}
        virtual void Shutdown() {}

        virtual void OnUpdate() {} // �� ������ ȣ��
        virtual void OnLateUpdate() {} // OnUpdate ���Ŀ� ȣ��

        virtual void OnBeginRender() {} // ������ ���� ȣ��
        virtual void OnRender() {} // ������
        virtual void OnRenderUI() {} // UI ������
        virtual void OnEndRender() {} // ������ ������ ȣ��

    protected:
        //EventBus m_eventBus; // �̺�Ʈ ����, ���ø����̼ǰ� ����� �� ��� �ϴ� �ּ�ó��
    };

}
