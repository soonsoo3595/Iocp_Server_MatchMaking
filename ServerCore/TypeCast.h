#pragma once
#include "Types.h"

// 이런 템플릿으로 타입 리스트를 만들면 컴파일 타임에 모든 것이 결정됨 -> 성능 오버헤드 X

#pragma region TypeList
// 원하는 개수의 인자를 받을 수 있다
template<typename... T>
struct TypeList;

// 아래 두 개는 템플릿 특수화를 이용해 두 버전 중 하나를 고르게

template<typename T, typename U>
struct TypeList<T, U>
{
	using Head = T;
	using Tail = U;
};

template<typename T, typename... U>
struct TypeList<T, U...>
{
	using Head = T;
	using Tail = TypeList<U...>;
};
#pragma endregion

#pragma region Length
// TypeList의 길이
template<typename T>
struct Length;

template<>
struct Length<TypeList<>>
{
	// enum도 값이 컴파일 타임에 결정됨
	enum { value = 0 };
};

// 재귀함수 느낌
template<typename T, typename... U>
struct Length<TypeList<T, U...>>
{
	enum { value = 1 + Length<TypeList<U...>>::value };
};
#pragma endregion

#pragma region TypeAt
// 타입 리스트가 있을 때 index에 해당하는 타입을 찾고 싶은 것
template<typename TL, int32 index>
struct TypeAt;

// 인덱스가 0인 경우는 Head
template<typename Head, typename... Tail>
struct TypeAt<TypeList<Head, Tail...>, 0>
{
	using Result = Head;
};

template<typename Head, typename... Tail, int32 index>
struct TypeAt<TypeList<Head, Tail...>, index>
{
	using Result = typename TypeAt<TypeList<Tail...>, index - 1>::Result;
};
#pragma endregion

#pragma  region IndexOf
// 어떤 타입을 주면 몇 번째 인덱스인지
template<typename TL, typename T>
struct IndexOf;

template<typename... Tail, typename T>
struct IndexOf<TypeList<T, Tail...>, T>
{
	enum { value = 0 };
};

// 못 찾은 경우
template<typename T>
struct IndexOf<TypeList<>, T>
{
	enum { value = -1 };
};

template<typename Head, typename... Tail, typename T>
struct IndexOf<TypeList<Head, Tail...>, T>
{
private:
	enum { temp = IndexOf<TypeList<Tail...>, T>::value };

public:
	enum { value = (temp == -1) ? -1 : temp + 1 };
};
#pragma endregion

#pragma region Conversion
// From에서 To로 변환이 가능한지
// 컴파일러가 여러가지 선택지가 있을 때 가장 그럴싸한 것을 채택한다는 특징을 이용
template<typename From, typename To>
class Conversion
{
private:
	using Small = __int8;
	using Big = __int32;

	static Small Test(const To&) { return 0; }
	static Big Test(...) { return 0; }
	// From을 반환
	static From MakeFrom() { return 0; }

public:
	enum
	{
		// From이 To로 변환이 가능하면 Small을 반환하는 Test 함수가 호출되어 Small을 반환해 true가 됨
		// 변환이 안되면 Big을 반환하여 false가 됨
		exists = sizeof(Test(MakeFrom())) == sizeof(Small)
	};
};
#pragma endregion

#pragma region TypeCast

// Int2Type<0>이랑 Int2Type<1>이랑 별개의 클래스로 인식돼서 컴파일 타임에 사용할 수 있게 됨
template<int32 v>
struct Int2Type
{
	enum { value = v };
};

// 타입이 n개 있을 때 n*n의 테이블 만들기
template<typename TL>
class TypeConversion
{
public:
	enum
	{
		length = Length<TL>::value
	};

	TypeConversion()
	{
		MakeTable(Int2Type<0>(), Int2Type<0>());
	}

	template<int32 i, int32 j>
	static void MakeTable(Int2Type<i>, Int2Type<j>)
	{
		// for문을 사용할 수가 없다. 컴파일 타임에 고정되어야 하기에 -> Int2Type
		// 테이블을 채우는 것 자체는 런타임
		// 식 자체는 컴파일 타임에 된다는 거임
		using FromType = typename TypeAt<TL, i>::Result;
		using ToType = typename TypeAt<TL, j>::Result;

		if (Conversion<const FromType*, const ToType*>::exists)
			s_convert[i][j] = true;
		else
			s_convert[i][j] = false;

		// 여기서 아예 다른 함수가 생성됨
		MakeTable(Int2Type<i>(), Int2Type<j + 1>());
	}

	// 멈춤
	template<int32 i>
	static void MakeTable(Int2Type<i>, Int2Type<length>)
	{
		MakeTable(Int2Type<i + 1>(), Int2Type<0>());
	}

	template<int j>
	static void MakeTable(Int2Type<length>, Int2Type<j>)
	{
	}

	static inline bool CanConvert(int32 from, int32 to)
	{
		static TypeConversion conversion;
		return s_convert[from][to];
	}

public:
	// length가 enum값이다 보니 배열에 넣을 수 있음
	static bool s_convert[length][length];
};

template<typename TL>
bool TypeConversion<TL>::s_convert[length][length];

// dynamic-cast 같은
template<typename To, typename From>
To TypeCast(From* ptr)
{
	if (ptr == nullptr)
		return nullptr;

	// From 클래스에 TL이 있어야 함. 어떤 타입 리스트를 사용할 것인지
	using TL = typename From::TL;

	if (TypeConversion<TL>::CanConvert(ptr->_typeId, IndexOf<TL, remove_pointer_t<To>>::value))
		return static_cast<To>(ptr);

	return nullptr;
}


template<typename To, typename From>
shared_ptr<To> TypeCast(shared_ptr<From> ptr)
{
	if (ptr == nullptr)
		return nullptr;

	using TL = typename From::TL;

	if (TypeConversion<TL>::CanConvert(ptr->_typeId, IndexOf<TL, remove_pointer_t<To>>::value))
		return static_pointer_cast<To>(ptr);

	return nullptr;
}

template<typename To, typename From>
bool CanCast(From* ptr)
{
	if (ptr == nullptr)
		return false;

	using TL = typename From::TL;
	return TypeConversion<TL>::CanConvert(ptr->_typeId, IndexOf<TL, remove_pointer_t<To>>::value);
}


template<typename To, typename From>
bool CanCast(shared_ptr<From> ptr)
{
	if (ptr == nullptr)
		return false;

	using TL = typename From::TL;
	return TypeConversion<TL>::CanConvert(ptr->_typeId, IndexOf<TL, remove_pointer_t<To>>::value);
}

#pragma endregion

#define DECLARE_TL		using TL = TL; int32 _typeId;
#define INIT_TL(Type)	_typeId = IndexOf<TL, Type>::value;