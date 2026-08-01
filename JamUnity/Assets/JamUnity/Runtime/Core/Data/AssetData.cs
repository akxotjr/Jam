using UnityEngine;

namespace JamUnity.Core.Data
{
    public interface IAssetData
    {
        string  AssetName { get; set; }
    }
    
    public interface IAssetData<TDto> : IAssetData
    {
        TDto ToDto();
        void FromDto(TDto dto);
    }
    
    public abstract class AssetData : ScriptableObject, IAssetData
    {
        [SerializeField] private string assetName;

        public virtual string AssetName
        {
            get => assetName?.Trim() ?? string.Empty;
            set => assetName = value?.Trim() ?? string.Empty;
        }

        protected void EnsureAssetNameInitialized()
        {
            if (string.IsNullOrWhiteSpace(assetName))
                assetName = name;
        }
    }
    
    public abstract class AssetData<TDto> : AssetData, IAssetData<TDto>
    {
        public abstract TDto ToDto();
        public abstract void FromDto(TDto dto);
    }
    
} // namespace JamUnity.Core.Data
