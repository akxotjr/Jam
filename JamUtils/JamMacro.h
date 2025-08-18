#pragma once

#define OUT

#define size16(val)		static_cast<int16>(sizeof(val))
#define size32(val)		static_cast<int32>(sizeof(val))
#define len16(arr)		static_cast<int16>(sizeof(arr) / sizeof(arr[0]))
#define len32(arr)		static_cast<int32>(sizeof(arr) / sizeof(arr[0]))

/*---------------
      Crash
---------------*/

#define CRASH(cause)						\
{											\
	uint32* crash = nullptr;				\
	__analysis_assume(crash != nullptr);	\
	*crash = 0xDEADBEEF;					\
}

#define ASSERT_CRASH(expr)			\
{									\
	if (!(expr))					\
	{								\
		CRASH("ASSERT_CRASH");		\
		__analysis_assume(expr);	\
	}								\
}


#define DECLARE_SINGLETON(ClassType)                            \
public:                                                         \
    static ClassType& Instance()                                \
    {                                                           \
        static ClassType instance;                              \
        return instance;                                        \
    }                                                           \
private:                                                        \
    ClassType() = default;                                      \
    ~ClassType() = default;                                     \
    ClassType(const ClassType&) = delete;                       \
    ClassType& operator=(const ClassType&) = delete;



#define USING_SHARED_PTR(name) using name##Ref = std::shared_ptr<class name>;

#define E2U(e) static_cast<std::underlying_type_t<decltype(e)>>(e)
#define U2E(EnumType, v) static_cast<EnumType>(v)
