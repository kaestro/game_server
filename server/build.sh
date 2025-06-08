#!/bin/bash

# C++ 게임 서버 빌드 스크립트

echo "=== C++ 게임 서버 빌드 스크립트 ==="

# CMake가 설치되어 있는지 확인
if ! command -v cmake &> /dev/null; then
    echo "오류: CMake가 설치되어 있지 않습니다."
    echo "CMake를 설치해주세요: sudo apt install cmake"
    exit 1
fi

echo "CMake 버전: $(cmake --version | head -n1)"

# 기존 빌드 디렉토리가 있으면 삭제할지 묻기
if [ -d "build" ]; then
    echo "기존 build 디렉토리가 존재합니다."
    read -p "기존 빌드를 삭제하고 새로 시작하시겠습니까? (y/N): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        echo "기존 build 디렉토리를 삭제 중..."
        rm -rf build
    fi
fi

# 빌드 디렉토리 생성
if [ ! -d "build" ]; then
    echo "build 디렉토리 생성 중..."
    mkdir build
fi

cd build

# CMake 설정
echo "CMake 설정 중..."
if ! cmake ..; then
    echo "CMake 설정 실패!"
    exit 1
fi

# 컴파일
echo "프로젝트 컴파일 중..."
if ! make; then
    echo "컴파일 실패!"
    exit 1
fi

echo "빌드 성공!"
echo "실행 파일: $(pwd)/MyGameServer"
echo
echo "서버 실행 방법:"
echo "  ./MyGameServer sync     # 동기 모드 (기본값)"
echo "  ./MyGameServer async    # 비동기 모드"
echo "  ./MyGameServer mtasync  # 멀티스레드 비동기 모드"
