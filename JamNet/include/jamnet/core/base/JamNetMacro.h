#pragma once
//
//#define OUT
//
//#define size16(val)		static_cast<int16>(sizeof(val))
//#define size32(val)		static_cast<int32>(sizeof(val))
//#define len16(arr)		static_cast<int16>(sizeof(arr) / sizeof(arr[0]))
//#define len32(arr)		static_cast<int32>(sizeof(arr) / sizeof(arr[0]))
//
///*---------------
//      Crash
//---------------*/
//
//#define JAM_CRASH(cause)					\
//{											\
//	uint32* crash = nullptr;				\
//	__analysis_assume(crash != nullptr);	\
//	*crash = 0xDEADBEEF;					\
//}
//
//#define JAM_ASSERT(expr)			        \
//{									        \
//	if (!(expr))					        \
//	{								        \
//		JAMNET_CRASH("ASSERT_CRASH");		\
//		__analysis_assume(expr);	        \
//	}								        \
//}
//
//
//
//#define DECLARE_SINGLETON(ClassType)                            \
//public:                                                         \
//    static ClassType& Instance()                                \
//    {                                                           \
//        static ClassType instance;                              \
//        return instance;                                        \
//    }                                                           \
//private:                                                        \
//    ClassType() = default;                                      \
//    ~ClassType() = default;                                     \
//    ClassType(const ClassType&) = delete;                       \
//    ClassType& operator=(const ClassType&) = delete;
//
//#define DECLARE_SINGLETON_INHERITANCE(ClassType)                \
//public:                                                         \
//    static ClassType& Instance()                                \
//    {                                                           \
//        static ClassType instance;                              \
//        return instance;                                        \
//    }                                                           \
//private:                                                        \
//    ClassType() = default;                                      \
//    ~ClassType() override = default;                            \
//    ClassType(const ClassType&) = delete;                       \
//    ClassType& operator=(const ClassType&) = delete;
//
//
//#define E2U(e) static_cast<std::underlying_type_t<decltype(e)>>(e)
//#define U2E(EnumType, v) static_cast<EnumType>(v)
//
//
//
//#define JAM_FORCE_INLINE __forceinline