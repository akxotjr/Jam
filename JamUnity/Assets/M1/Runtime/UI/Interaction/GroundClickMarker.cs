using UnityEngine;
using System.Collections;

public class GroundClickMarker : MonoBehaviour
{
    [SerializeField]
    private ParticleSystem clickParticleSystem;

    [SerializeField]
    private float heightOffset = 0.02f;

    [SerializeField]
    private float visibleDuration = 0.5f;

    private Coroutine hideRoutine;

    private void Awake()
    {
        gameObject.SetActive(false);
    }

    public void Show(Vector3 position, Vector3 normal)
    {
        if (GetComponent<ParticleSystem>() == null)
            return;

        if (!gameObject.activeSelf)
            gameObject.SetActive(true);

        transform.SetPositionAndRotation(position + normal * heightOffset, Quaternion.FromToRotation(Vector3.up, normal));

        if (hideRoutine != null)
            StopCoroutine(hideRoutine);

        GetComponent<ParticleSystem>().gameObject.SetActive(true);
        var main = GetComponent<ParticleSystem>().main;
        main.loop = false;
        GetComponent<ParticleSystem>().Stop(true, ParticleSystemStopBehavior.StopEmittingAndClear);
        GetComponent<ParticleSystem>().Play(true);
        hideRoutine = StartCoroutine(HideAfterPlayback());
    }

    private IEnumerator HideAfterPlayback()
    {
        yield return new WaitForSeconds(visibleDuration);

        if (GetComponent<ParticleSystem>() != null)
        {
            GetComponent<ParticleSystem>().Stop(true, ParticleSystemStopBehavior.StopEmittingAndClear);
        }

        hideRoutine = null;
        gameObject.SetActive(false);
    }
}
