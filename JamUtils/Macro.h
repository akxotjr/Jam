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

#define DECLARE_SINGLETON(ClassType)                      \
public:                                                   \
    static void PreInit()                                 \
    {                                                     \
        if (!_instance)                                   \
            _instance = std::make_shared<ClassType>();    \
    }                                                     \
    static void Shutdown()                                \
    {                                                     \
        _instance.reset();                                \
    }                                                     \
    static std::shared_ptr<ClassType> Instance()          \
    {                                                     \
        return _instance;                                 \
    }                                                     \
private:                                                  \
    static inline std::shared_ptr<ClassType> _instance;


#define USING_SHARED_PTR(name) using name##Ref = std::shared_ptr<class name>;