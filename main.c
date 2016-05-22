/********************************************************************
                            SNAKE GAME

                    Programmer: André Vicente Milack
                    Email: andrevicente.m@gmail.com
********************************************************************/

#include <windows.h>
#include <stdlib.h>
#include <time.h>
#include "resources.h"

/********************************************************************
                        DEFINES & MACROS
********************************************************************/

#define FIELD_WIDTH                 21    //Max: 127
#define FIELD_HEIGHT                15    //Max: 127
#define BORDER_WIDTH                1
#define BORDER_COLOR                RGB(0xFF, 0xFF, 0x00)
#define EMPTY_BLOCK_COLOR           RGB(0x00, 0xC0, 0x00)
#define FOOD_BLOCK_COLOR            RGB(0xC0, 0x00, 0x00)
#define SNAKE_HEAD_COLOR            RGB(0x20, 0x20, 0x20)
#define SNAKE_BODY_COLOR            RGB(0x40, 0x40, 0x40)
#define SNAKE_SPEED                 15    //Blocks per second
#define INITIAL_DIRECTION           RIGHT
#define INITIAL_SNAKE_SIZE          5
#define PASS_THROUGH_WALLS          FALSE

#define BLOCK_X(p)                  ((char) (p & 0x00FF))
#define BLOCK_Y(p)                  ((char) ((p >> 8) & 0x00FF))
#define BLOCK_POSITION(x, y)        ((WORD) ((x & 0x00FF) | ((y << 8) & 0xFF00)))
#define BLOCK_BUFFER_POSITION(p)    ((int) BLOCK_Y(p) * fieldWidth + (int) BLOCK_X(p))
#define OPPOSITE_DIRECTION(d)       ((SNAKE_DIRECTION) (((int) d + 2) & 3))
#define IS_PERPENDICULAR(d1, d2)    ((BOOL) ((d1 ^ d2) & 0x01))
#define IS_BLOCK_AVAILABLE(s)       ((BOOL) (~s & 0x02))

/********************************************************************
                            ENUMS & STRUCTS
********************************************************************/

typedef enum _BLOCK_STATE
{
    EMPTY       = 0,
    FOOD        = 1,
    SNAKE_HEAD  = 2,
    SNAKE_BODY  = 3
} BLOCK_STATE;

typedef enum _SNAKE_DIRECTION
{
    RIGHT   = 0,
    UP      = 1,
    LEFT    = 2,
    DOWN    = 3
} SNAKE_DIRECTION;

typedef enum _SNAKE_RESULT
{
    SR_OK = 0,                  //No error.
    SR_MEMORY_ERROR,            //An error occurred while allocating memory.
    SR_BAD_FIELD_SIZE,          //Field width and height must be between 0 and 127.
    SR_BAD_SNAKE_SIZE,          //Snake size must be greater than zero.
    SR_BAD_INITIAL_POSITION,    //Snake doesn't fit into the field. Change the initial position or the initial direction.
    SR_BAD_SNAKE_SPEED,         //The speed of the snake must be a positive integer.
    SR_NO_SPACE_FOR_FOOD        //There is no empty space for the food.
} SNAKE_RESULT;

typedef enum _SNAKE_STATE
{
    IDLE,
    RUNNING,
    PAUSED,
    LOST,
    WON
} SNAKE_STATE;

typedef struct _SNAKE_ELEMENT
{
    WORD blockPosition;
    GLOBALHANDLE hNextElement;
} SNAKE_ELEMENT;

typedef struct _DIRECTION_COMMAND
{
    SNAKE_DIRECTION direction;
    GLOBALHANDLE hNextCommand;
} DIRECTION_COMMAND;

/********************************************************************
                        GLOBAL VARIABLES
********************************************************************/

UINT fieldWidth = FIELD_WIDTH;
UINT fieldHeight = FIELD_HEIGHT;
UINT snakeSpeed = SNAKE_SPEED;
BOOL passThroughWalls = PASS_THROUGH_WALLS;

GLOBALHANDLE hFieldBuffer;
GLOBALHANDLE hSnakeStack;
GLOBALHANDLE hCommandsBeginning;
GLOBALHANDLE hCommandsEnding;
SNAKE_DIRECTION previousDirection;
SNAKE_STATE snakeState;
UINT emptyBlocks, snakeSize;

/********************************************************************
                        SNAKE CORE FUNCTIONS
********************************************************************/

GLOBALHANDLE CreateSnakeBlock(WORD BlockPosition, GLOBALHANDLE NextElement)
{
    GLOBALHANDLE hNewSnakeBlock;
    SNAKE_ELEMENT *pNewSnakeBlock;

    hNewSnakeBlock = GlobalAlloc(GMEM_MOVEABLE, sizeof(SNAKE_ELEMENT));
    
    if (hNewSnakeBlock)
    {
        pNewSnakeBlock = (SNAKE_ELEMENT *) GlobalLock(hNewSnakeBlock);

        pNewSnakeBlock->blockPosition = BlockPosition;
        pNewSnakeBlock->hNextElement = NextElement;

        GlobalUnlock(hNewSnakeBlock);
    }

    return hNewSnakeBlock;
}

void DestroySnakeStack(GLOBALHANDLE SnakeStack)
{
    GLOBALHANDLE hNextElement = SnakeStack;
    GLOBALHANDLE hCurrentElement;
    SNAKE_ELEMENT *pCurrentElement;

    while (hNextElement)
    {
        hCurrentElement = hNextElement;
        pCurrentElement = (SNAKE_ELEMENT *) GlobalLock(hCurrentElement);
        hNextElement    = pCurrentElement->hNextElement;
        
		GlobalUnlock(hCurrentElement);
        GlobalFree(hCurrentElement);
    }
}

WORD NewPosition(WORD Position, char Steps, SNAKE_DIRECTION Direction)
{
    char x, y;

    x = BLOCK_X(Position);
    y = BLOCK_Y(Position);

    switch (Direction)
    {
        case RIGHT:
            if (passThroughWalls) x = (x + Steps) % fieldWidth;
            else x += Steps;
            break;

        case UP:
            if (passThroughWalls) y = (y + Steps) % fieldHeight;
            else y += Steps;
            break;

        case LEFT:
            if (passThroughWalls) x = (x + fieldWidth - Steps) % fieldWidth;
            else x -= Steps;
            break;

        case DOWN:
            if (passThroughWalls) y = (y + fieldHeight - Steps) % fieldHeight;
            else y -= Steps;
            break;
    }

    return BLOCK_POSITION(x, y);
}

BOOL IsInsideField(WORD Position)
{
    BYTE x, y;

    x = BLOCK_X(Position);
    y = BLOCK_Y(Position);

    return x < fieldWidth && y < fieldHeight;
}

BLOCK_STATE GetFieldBlock(WORD Position)
{
    BYTE        *pFieldBuffer;
    BLOCK_STATE state;
    BYTE        *pBlockByte;
    int         shift;
    int         bufferPosition = BLOCK_BUFFER_POSITION(Position);

    pFieldBuffer = (BYTE *) GlobalLock(hFieldBuffer);
    pBlockByte   = pFieldBuffer + bufferPosition / 4;
    shift        = 2 * (bufferPosition % 4);
    state        = (BLOCK_STATE) ((*pBlockByte >> shift) & 0x03);

    GlobalUnlock(hFieldBuffer);
    return state;
}

void SetFieldBlock(WORD Position, BLOCK_STATE NewState)
{
    BYTE        *pFieldBuffer;
    BLOCK_STATE previousState;
    BYTE        *pBlockByte;
    BYTE        mask;
    int         shift;
    int         bufferPosition = BLOCK_BUFFER_POSITION(Position);

    pFieldBuffer = (BYTE *) GlobalLock(hFieldBuffer);
    pBlockByte   = pFieldBuffer + bufferPosition / 4;

    shift         = 2 * (bufferPosition % 4);
    mask          = 0x03 << shift;
    previousState = (BLOCK_STATE) ((*pBlockByte & mask) >> shift);

    *pBlockByte &= ~mask;
    *pBlockByte |= (NewState << shift) & mask;

    GlobalUnlock(hFieldBuffer);

    if (previousState == EMPTY) emptyBlocks--;
    if (NewState      == EMPTY) emptyBlocks++;
}

SNAKE_RESULT BuildSnakeStack()
{
    WORD            tailPosition, headPosition;
    SNAKE_DIRECTION tailDirection = OPPOSITE_DIRECTION(INITIAL_DIRECTION);
    WORD            currentPosition;
    GLOBALHANDLE    hPreviousBlock;
    GLOBALHANDLE    hCurrentBlock;
    SNAKE_ELEMENT   *pCurrentBlock;
    int i;

    //Check if the snake has an appropriate size
    if (INITIAL_SNAKE_SIZE == 0) return SR_BAD_SNAKE_SIZE;
    
    //Calculate head position
    headPosition = BLOCK_POSITION(INITIAL_SNAKE_SIZE + (fieldWidth - INITIAL_SNAKE_SIZE) / 3 - 1, (fieldHeight - 1) / 2);

    //Check if the snake's both head and tail are inside the field
    if (!IsInsideField(headPosition))
        return SR_BAD_INITIAL_POSITION;
    
    tailPosition = NewPosition(headPosition,
                               INITIAL_SNAKE_SIZE - 1,
                               tailDirection);

    if (!IsInsideField(tailPosition))
        return SR_BAD_INITIAL_POSITION;

    //Create the head of the snake
    hCurrentBlock   = GlobalAlloc(GMEM_MOVEABLE, sizeof(SNAKE_ELEMENT));
    currentPosition = headPosition;

    if (!hCurrentBlock) return SR_MEMORY_ERROR;

    pCurrentBlock                = (SNAKE_ELEMENT *) GlobalLock(hCurrentBlock);
    pCurrentBlock->blockPosition = headPosition;
    pCurrentBlock->hNextElement  = NULL;
	
    GlobalUnlock(hCurrentBlock);

    SetFieldBlock(headPosition, SNAKE_HEAD);

    //Create the remaining body elements
    for (i = 1; i < INITIAL_SNAKE_SIZE; i++)
    {
        hPreviousBlock  = hCurrentBlock;
        currentPosition = NewPosition(currentPosition, 1, tailDirection);
        hCurrentBlock   = GlobalAlloc(GMEM_MOVEABLE, sizeof(SNAKE_ELEMENT));

        if (!hCurrentBlock)
        {
            DestroySnakeStack(hPreviousBlock);
            return SR_MEMORY_ERROR;
        }

        pCurrentBlock                = (SNAKE_ELEMENT *) GlobalLock(hCurrentBlock);
        pCurrentBlock->blockPosition = currentPosition;
        pCurrentBlock->hNextElement  = hPreviousBlock;
        GlobalUnlock(hCurrentBlock);

        SetFieldBlock(currentPosition, SNAKE_BODY);
    }

    hSnakeStack = hCurrentBlock;
    snakeSize   = INITIAL_SNAKE_SIZE;
    return SR_OK;
}

void CreateNewFood()
{
    BLOCK_STATE state;
    BYTE        *pBlockByte;
    BYTE        mask;
    int         fieldBytes = (fieldWidth * fieldHeight + 3) / 4;
    int         foodIndex = rand() % emptyBlocks;
    int         currentIndex = 0;
    int         i, j;

    pBlockByte = (BYTE *) GlobalLock(hFieldBuffer);

    //Sweep all the bytes of the field buffer
    for (i = 0; i < fieldBytes; i++)
    {
        //Sweep all the bit pairs of each byte
        for (j = 0; j < 4; j++)
        {
            mask  = 0x03 << 2 * j;
            state = (BLOCK_STATE) ((*pBlockByte & mask) >> 2 * j);

            if (state == EMPTY)
            {
                if (currentIndex == foodIndex)
                {
                    *pBlockByte &= ~mask;
                    *pBlockByte |= (FOOD << 2 * j) & mask;
                    emptyBlocks--;

                    i = fieldBytes;
                    break;
                }

                currentIndex++;
            }
        }

        pBlockByte++;
    }

    GlobalUnlock(hFieldBuffer);
}

SNAKE_RESULT ReceiveCommand(SNAKE_DIRECTION Direction)
{
    GLOBALHANDLE hNewCommand = NULL;
    DIRECTION_COMMAND *pNewCommand;
    DIRECTION_COMMAND *pPreviousCommand;
    SNAKE_DIRECTION lastDirection;
    
    if (snakeState != RUNNING) return SR_OK;
    
    if (hCommandsEnding == NULL) lastDirection = previousDirection;
    else
    {
        pPreviousCommand = (DIRECTION_COMMAND *) GlobalLock(hCommandsEnding);
        lastDirection    = pPreviousCommand->direction;
    }
    
    if (IS_PERPENDICULAR(Direction, lastDirection))
    {
        hNewCommand = GlobalAlloc(GMEM_MOVEABLE, sizeof(DIRECTION_COMMAND));
        
        if (hNewCommand == NULL)
        {
            if (hCommandsEnding != NULL) GlobalUnlock(hCommandsEnding);
            return SR_MEMORY_ERROR;
        }
        
        pNewCommand               = (DIRECTION_COMMAND *) GlobalLock(hNewCommand);
        pNewCommand->hNextCommand = NULL;
        pNewCommand->direction    = Direction;
        
		GlobalUnlock(hNewCommand);
    }
    
    if (hCommandsEnding == NULL)
    {
        if (hNewCommand != NULL)
        {
            hCommandsBeginning = hNewCommand;
            hCommandsEnding    = hNewCommand;
        }
    }
    else
    {
        if (hNewCommand != NULL)
        {
            pPreviousCommand->hNextCommand = hNewCommand;
            hCommandsEnding                = hNewCommand;
        }
        
        GlobalUnlock(hCommandsEnding);
    }
    
    return SR_OK;
}

SNAKE_DIRECTION PickDirection()
{
    DIRECTION_COMMAND *pFirstCommand;
    GLOBALHANDLE      hSecondCommand;
    SNAKE_DIRECTION   ret;
    
    if (hCommandsBeginning == NULL) return previousDirection;
    
    pFirstCommand  = (DIRECTION_COMMAND *) GlobalLock(hCommandsBeginning);
    ret            = pFirstCommand->direction;
    hSecondCommand = pFirstCommand->hNextCommand;
	
    GlobalUnlock(hCommandsBeginning);
    GlobalFree(hCommandsBeginning);
    
    if (hSecondCommand == NULL)
    {
        hCommandsBeginning = NULL;
        hCommandsEnding    = NULL;
    }
    else hCommandsBeginning = hSecondCommand;
    
    return ret;
}

void DestroyCommandsList(GLOBALHANDLE CommandsList)
{
    GLOBALHANDLE      hNextCommand = CommandsList;
    GLOBALHANDLE      hCurrentCommand;
    DIRECTION_COMMAND *pCurrentCommand;

    while (hNextCommand)
    {
        hCurrentCommand = hNextCommand;
        pCurrentCommand = (DIRECTION_COMMAND *) GlobalLock(hCurrentCommand);
        hNextCommand    = pCurrentCommand->hNextCommand;
        
		GlobalUnlock(hCurrentCommand);
        GlobalFree(hCurrentCommand);
    }
}

SNAKE_RESULT MoveSnake()
{
    GLOBALHANDLE  hNextElement = hSnakeStack;
    GLOBALHANDLE  hCurrentElement;
    SNAKE_ELEMENT *pCurrentElement, *pNextElement;
    WORD          nextPosition, tailPreviousPosition;
    BLOCK_STATE   nextState;
    BOOL          isInside, isAvailable, gotFood, isTail = TRUE;

    while (hNextElement)
    {
        hCurrentElement = hNextElement;
        pCurrentElement = (SNAKE_ELEMENT *) GlobalLock(hCurrentElement);
		
        if (isTail) tailPreviousPosition = pCurrentElement->blockPosition;
		
        hNextElement = pCurrentElement->hNextElement;
    
        if (hNextElement)
        {
            pNextElement = (SNAKE_ELEMENT *) GlobalLock(hNextElement);
			
            if (isTail) SetFieldBlock(pCurrentElement->blockPosition, EMPTY);
			
            pCurrentElement->blockPosition = pNextElement->blockPosition;
            GlobalUnlock(hNextElement);
        }
        else //Current element is the head
        {
            previousDirection = PickDirection();
            nextPosition      = NewPosition(pCurrentElement->blockPosition, 1, previousDirection);
            
            if (isTail) SetFieldBlock(pCurrentElement->blockPosition, EMPTY);
            else        SetFieldBlock(pCurrentElement->blockPosition, SNAKE_BODY);
            
            if (isInside = IsInsideField(nextPosition))
            {
                nextState   = GetFieldBlock(nextPosition);
                isAvailable = IS_BLOCK_AVAILABLE(nextState);
                gotFood     = nextState == FOOD;

                pCurrentElement->blockPosition = nextPosition;
                SetFieldBlock(nextPosition, SNAKE_HEAD);

                if (gotFood)
                {
                    hSnakeStack = CreateSnakeBlock(tailPreviousPosition, hSnakeStack);
                    
                    if (hSnakeStack == NULL)
                    {
                        GlobalUnlock(hCurrentElement);
                        return SR_MEMORY_ERROR;
                    }
                    
                    SetFieldBlock(tailPreviousPosition, SNAKE_BODY);
                    snakeSize++;
                    
                    if (emptyBlocks) CreateNewFood();
                    else snakeState = WON;
                }
            }

            if (!isInside || !isAvailable) snakeState = LOST;
        }

        GlobalUnlock(hCurrentElement);
		
        if (isTail) isTail = FALSE;
    }
    
    return SR_OK;
}

SNAKE_RESULT Initialize(BOOL EmptyField)
{
    SNAKE_RESULT result;

    //Create field
    if (fieldWidth <= 0 || fieldWidth > 127 || fieldHeight <= 0 || fieldHeight > 127)
        return SR_BAD_FIELD_SIZE;
    
    if (snakeSpeed <= 0)
        return SR_BAD_SNAKE_SPEED;

    if (!(hFieldBuffer = GlobalAlloc(GHND, (fieldWidth * fieldHeight + 3) / 4)))
        return SR_MEMORY_ERROR;

    emptyBlocks = fieldWidth * fieldHeight;
    
    //Initialize direction commands list
    hCommandsBeginning = NULL;
    hCommandsEnding    = NULL;
    previousDirection  = INITIAL_DIRECTION;
    
    if (EmptyField)
    {
        hSnakeStack = NULL;
        snakeState  = IDLE;
        snakeSize   = 0;
    }
    else
    {
        //Create snake
        if (result = BuildSnakeStack())
        {
            GlobalFree(hFieldBuffer);
            hFieldBuffer = NULL;
            return result;
        }

        //Create first food block
        if (emptyBlocks) CreateNewFood();
        else
        {
            DestroySnakeStack(hSnakeStack);
            hSnakeStack = NULL;
            GlobalFree(hFieldBuffer);
            hFieldBuffer = NULL;
            return SR_NO_SPACE_FOR_FOOD;
        }
        
        snakeState = RUNNING;
    }

    return SR_OK;
}

void EndingCleanUp()
{
    if (hSnakeStack != NULL)
    {
        DestroySnakeStack(hSnakeStack);
        hSnakeStack = NULL;
    }
    
    if (hFieldBuffer != NULL)
    {
        GlobalFree(hFieldBuffer);
        hFieldBuffer = NULL;
    }
    
    if (hCommandsBeginning != NULL)
    {
        DestroyCommandsList(hCommandsBeginning);
        hCommandsBeginning = NULL;
        hCommandsEnding    = NULL;
    }
}

const LPCTSTR ResultToString(SNAKE_RESULT Result)
{
    const LPCTSTR strings[] =
    {
        /* SR_OK */                      TEXT("No error."),
        /* SR_MEMORY_ERROR */            TEXT("An error occurred while allocating memory."),
        /* SR_BAD_FIELD_SIZE */          TEXT("Field width and height must be between 1 and 127."),
        /* SR_BAD_SNAKE_SIZE */          TEXT("Snake size must be greater than zero."),
        /* SR_BAD_INITIAL_POSITION */    TEXT("Snake doesn't fit into the field."),
        /* SR_BAD_SNAKE_SPEED */         TEXT("The speed of the snake must be a positive integer."),
        /* SR_NO_SPACE_FOR_FOOD */       TEXT("There is no empty space for the food.")
    };

    return strings[(int) Result];
}

/********************************************************************
                        RENDER FUNCTIONS
********************************************************************/

RECT CalculateFieldRect(RECT ClientRect, TEXTMETRIC TextMetric)
{
    int  clientWidth;
    int  clientHeight;
    int  blockAreaWidth;
    int  blockAreaHeight;
    int  fieldDelta;
    RECT fieldRect;
    
    ClientRect.bottom -= TextMetric.tmHeight + 3;
    
    clientWidth     = ClientRect.right - ClientRect.left;
    clientHeight    = ClientRect.bottom - ClientRect.top;
    blockAreaWidth  = clientWidth  - (fieldWidth + 1) * BORDER_WIDTH;
    blockAreaHeight = clientHeight - (fieldHeight + 1) * BORDER_WIDTH;
    
    if (blockAreaWidth * fieldHeight >= blockAreaHeight * fieldWidth)
    //Block area is wider than the field
    {
        fieldRect.top    = 0;
        fieldRect.bottom = clientHeight;

        fieldDelta = blockAreaHeight * fieldWidth / fieldHeight + (fieldWidth + 1) * BORDER_WIDTH;

        fieldRect.left  = (clientWidth - fieldDelta) / 2;
        fieldRect.right = fieldRect.left + fieldDelta;
    }
    else
    //Block area is higher than the field
    {
        fieldRect.left  = 0;
        fieldRect.right = clientWidth;

        fieldDelta = blockAreaWidth * fieldHeight / fieldWidth + (fieldHeight + 1) * BORDER_WIDTH;

        fieldRect.top    = (clientHeight - fieldDelta) / 2;
        fieldRect.bottom = fieldRect.top + fieldDelta;
    }
    
    return fieldRect;
}

void RenderSnake(HWND HWnd)
{
    HDC         hdcWindow, hdcMemory;
    HBITMAP     hbmMemory;
    RECT        clientRect, fieldRect, blockRect;
    HBRUSH      borderBrush, blockBrush, foodBrush, headBrush, bodyBrush;
    HBRUSH      brushArray[4];
    BLOCK_STATE state;
    UINT        x, y, usableWidth, usableHeight, accumBorders;
    TEXTMETRIC  textMetric;
    TCHAR       bottomText[32];

    hdcWindow = GetDC(HWnd);

    //Calculate measures
    GetTextMetrics(hdcWindow, &textMetric);
    GetClientRect(HWnd, &clientRect);
    fieldRect    = CalculateFieldRect(clientRect, textMetric);
    usableWidth  = fieldRect.right - fieldRect.left - (fieldWidth + 1) * BORDER_WIDTH;
    usableHeight = fieldRect.bottom - fieldRect.top - (fieldHeight + 1) * BORDER_WIDTH;

    //Create memory device context
    hdcMemory = CreateCompatibleDC(hdcWindow);
    hbmMemory = CreateCompatibleBitmap(hdcWindow, clientRect.right - clientRect.left, clientRect.bottom - clientRect.top);
    SelectObject(hdcMemory, hbmMemory);

    //Create brushes
    borderBrush = CreateSolidBrush(BORDER_COLOR);
    blockBrush  = CreateSolidBrush(EMPTY_BLOCK_COLOR);
    foodBrush   = CreateSolidBrush(FOOD_BLOCK_COLOR);
    headBrush   = CreateSolidBrush(SNAKE_HEAD_COLOR);
    bodyBrush   = CreateSolidBrush(SNAKE_BODY_COLOR);

    brushArray[0] = blockBrush;
    brushArray[1] = foodBrush;
    brushArray[2] = headBrush;
    brushArray[3] = bodyBrush;

    //Paint borders and background
    FillRect(hdcMemory, &clientRect, (HBRUSH) GetStockObject(WHITE_BRUSH));
    FillRect(hdcMemory, &fieldRect, borderBrush);
    
    //Draw bottom bar
    SelectObject(hdcMemory, GetStockObject(BLACK_PEN));
    MoveToEx(hdcMemory, 0, clientRect.bottom - textMetric.tmHeight - 3, NULL);
    LineTo(hdcMemory, clientRect.right, clientRect.bottom - textMetric.tmHeight - 3);
    
    SetTextAlign(hdcMemory, TA_LEFT);
    TextOut(hdcMemory, 2, clientRect.bottom - textMetric.tmHeight - 2, bottomText,
            wsprintf(bottomText, TEXT("Snake Size: %u"), snakeSize));
            
    SetTextAlign(hdcMemory, TA_CENTER);
    TextOut(hdcMemory, clientRect.right / 2, clientRect.bottom - textMetric.tmHeight - 2, bottomText,
            wsprintf(bottomText, TEXT("%u x %u"), fieldWidth, fieldHeight));
    
    SetTextAlign(hdcMemory, TA_RIGHT);
    TextOut(hdcMemory, clientRect.right - 2, clientRect.bottom - textMetric.tmHeight - 2, bottomText,
            wsprintf(bottomText, TEXT("Speed: %u"), snakeSpeed));

    //Paint blocks
    if (hFieldBuffer)
    {
        for (x = 0; x < fieldWidth; x++)
        {
            for (y = 0; y < fieldHeight; y++)
            {
                state = GetFieldBlock(BLOCK_POSITION(x, y));

                accumBorders    = fieldRect.left + (x + 1) * BORDER_WIDTH;
                blockRect.left  = accumBorders + x * usableWidth / fieldWidth;
                blockRect.right = accumBorders + (x + 1) * usableWidth / fieldWidth;

                accumBorders     = fieldRect.bottom - (y + 1) * BORDER_WIDTH;
                blockRect.top    = accumBorders - y * usableHeight / fieldHeight;
                blockRect.bottom = accumBorders - (y + 1) * usableHeight / fieldHeight;

                FillRect(hdcMemory, &blockRect, brushArray[(int) state]);
            }
        }
    }
    else
    {
        for (x = 0; x < fieldWidth; x++)
        {
            for (y = 0; y < fieldHeight; y++)
            {
                accumBorders    = fieldRect.left + (x + 1) * BORDER_WIDTH;
                blockRect.left  = accumBorders + x * usableWidth / fieldWidth;
                blockRect.right = accumBorders + (x + 1) * usableWidth / fieldWidth;

                accumBorders     = fieldRect.bottom - (y + 1) * BORDER_WIDTH;
                blockRect.top    = accumBorders - y * usableHeight / fieldHeight;
                blockRect.bottom = accumBorders - (y + 1) * usableHeight / fieldHeight;

                FillRect(hdcMemory, &blockRect, blockBrush);
            }
        }
    }
    

    BitBlt(hdcWindow, 0, 0, clientRect.right - clientRect.left, clientRect.bottom - clientRect.top,
           hdcMemory, 0, 0, SRCCOPY);

    //Clean-up and finish
    DeleteObject(borderBrush);
    DeleteObject(blockBrush);
    DeleteObject(foodBrush);
    DeleteObject(headBrush);
    DeleteObject(bodyBrush);

    DeleteObject(hbmMemory);
    DeleteDC(hdcMemory);

    ReleaseDC(HWnd, hdcWindow);
    ValidateRect(HWnd, &clientRect);
}

/********************************************************************
                        WINDOWS FUNCTIONS
********************************************************************/

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
BOOL    CALLBACK AboutDlgProc(HWND HWnd, UINT Message, WPARAM WParam, LPARAM LParam);
BOOL    CALLBACK PreferencesDlgProc(HWND HWnd, UINT Message, WPARAM WParam, LPARAM LParam);
BOOL             HandleCommand(HINSTANCE HInstance, HWND HWnd, WPARAM WParam);
BOOL             HandleKeyDown(HWND HWnd, WPARAM WParam);
BOOL             ReadIntFromString(TCHAR *String, int *Result);
void             CriticalEnd(HWND HWnd, SNAKE_RESULT Result);

int WINAPI WinMain(HINSTANCE HInstance, HINSTANCE HPrevInstance, PSTR CmdLine, int CmdShow)
{
    static TCHAR appName[] = TEXT("Snake");
	
    HWND         hwnd;
    MSG          msg;
    WNDCLASSEX   wndclass;
    HANDLE       hAccel;
    
    srand((UINT) time(NULL));

    wndclass.cbSize         = sizeof(WNDCLASSEX);
    wndclass.style          = CS_HREDRAW | CS_VREDRAW;
    wndclass.lpfnWndProc    = WndProc;
    wndclass.cbClsExtra     = 0;
    wndclass.cbWndExtra     = 0;
    wndclass.hInstance      = HInstance;
    wndclass.hIcon          = LoadIcon(HInstance, MAKEINTRESOURCE(IDI_ICON32));
    wndclass.hIconSm        = LoadIcon(HInstance, MAKEINTRESOURCE(IDI_ICON16));
    wndclass.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wndclass.hbrBackground  = (HBRUSH) GetStockObject(WHITE_BRUSH);
    wndclass.lpszMenuName   = MAKEINTRESOURCE(IDM_MENU);
    wndclass.lpszClassName  = appName;
    
    if (!RegisterClassEx(&wndclass))
    {
        MessageBox(NULL, TEXT("It's not possible to register window class."), appName, MB_ICONERROR);
        return 0 ;
    }
    
    hwnd = CreateWindow(appName,                // window class name
                        TEXT("Snake"),          // window caption
                        WS_OVERLAPPEDWINDOW,    // window style
                        CW_USEDEFAULT,          // initial x position
                        CW_USEDEFAULT,          // initial y position
                        747,                    // initial x size
                        600,                    // initial y size
                        NULL,                   // parent window handle
                        NULL,                   // window menu handle
                        HInstance,              // program instance handle
                        NULL);                  // creation parameters

    ShowWindow(hwnd, CmdShow);
    UpdateWindow(hwnd);
    
    hAccel = LoadAccelerators(HInstance, MAKEINTRESOURCE(IDK_ACCELERATORS));
    
    while (GetMessage(&msg, NULL, 0, 0))
    {
        if (!TranslateAccelerator(hwnd, hAccel, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    
    return msg.wParam;
}

LRESULT CALLBACK WndProc(HWND HWnd, UINT Message, WPARAM WParam, LPARAM LParam)
{
    static HINSTANCE hInstance;
    
    SNAKE_RESULT result;
    TCHAR        string[100];

    switch (Message)
    {
        case WM_CREATE:
            hInstance = ((LPCREATESTRUCT) LParam)->hInstance;
            if ((result = Initialize(TRUE)) != SR_OK) CriticalEnd(HWnd, result);
            return 0;
            
        case WM_COMMAND:
            if (HandleCommand(hInstance, HWnd, WParam)) return 0;
            else return DefWindowProc(HWnd, Message, WParam, LParam);

        case WM_ERASEBKGND:
            return 0;

        case WM_PAINT:
            RenderSnake(HWnd);
            return 0;

        case WM_KEYDOWN:
            if (HandleKeyDown(HWnd, WParam)) return 0;
            else return DefWindowProc(HWnd, Message, WParam, LParam);

        case WM_TIMER:
            if ((result = MoveSnake()) == SR_OK)
            {
                if (snakeState != RUNNING)
                {
                    KillTimer(HWnd, 1);
                    
                    if (snakeState == LOST) wsprintf(string, TEXT("You lost!"));
                    else wsprintf(string, TEXT("Congratulations! The snake filled all the empty spaces, good job!"));
                    
                    MessageBox(HWnd, string, TEXT("Snake"), MB_OK | MB_ICONINFORMATION);
                }
            }
            else CriticalEnd(HWnd, result);
            InvalidateRect(HWnd, NULL, FALSE);
            return 0;

        case WM_DESTROY:
            KillTimer(HWnd, 1);
            EndingCleanUp();
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProc(HWnd, Message, WParam, LParam);
}

BOOL CALLBACK AboutDlgProc(HWND HDlg, UINT Message, WPARAM WParam, LPARAM LParam)
{
    switch (Message)
    {
        case WM_INITDIALOG:
            return TRUE;
        
        case WM_COMMAND:
            if (LOWORD(WParam) == IDOK || LOWORD(WParam) == IDCANCEL) EndDialog(HDlg, 0);
            return TRUE;
        
        case WM_CLOSE:
            EndDialog(HDlg, 0);
            return TRUE;
    }
    
    return FALSE;
}

BOOL CALLBACK PreferencesDlgProc(HWND HDlg, UINT Message, WPARAM WParam, LPARAM LParam)
{
    static HWND hwndParent, hwndWidth, hwndHeight, hwndSpeed, hwndPassWallThrough;
    
	TCHAR        editText[10], messageText[80];
    UINT         editWidth, editHeight, editSpeed;
    BOOL         checkPassThroughWalls;
    BOOL         success;
    int          dialogResult;
    SNAKE_RESULT result;
    
    switch (Message)
    {
        case WM_INITDIALOG:
            hwndParent          = GetParent(HDlg);
            hwndWidth           = GetDlgItem(HDlg, IDC_FIELD_WIDTH);
            hwndHeight          = GetDlgItem(HDlg, IDC_FIELD_HEIGHT);
            hwndSpeed           = GetDlgItem(HDlg, IDC_SPEED);
            hwndPassWallThrough = GetDlgItem(HDlg, IDC_PASS_THROUGH_WALLS);
            
            wsprintf(editText, TEXT("%u"), fieldWidth);
            SetWindowText(hwndWidth, editText);
            
            wsprintf(editText, TEXT("%u"), fieldHeight);
            SetWindowText(hwndHeight, editText);
            
            wsprintf(editText, TEXT("%u"), snakeSpeed);
            SetWindowText(hwndSpeed, editText);
            
            CheckDlgButton(HDlg, IDC_PASS_THROUGH_WALLS, passThroughWalls);
            return TRUE;
        
        case WM_COMMAND:
            if (LOWORD(WParam) == IDOK)
            {
                GetWindowText(hwndWidth, editText, 10);
                success = ReadIntFromString(editText, &editWidth);
                if (!success || editWidth < INITIAL_SNAKE_SIZE || editWidth > 127)
                {
                    wsprintf(messageText, TEXT("Invalid field width. Please, enter a numeric value between %u and 127."), INITIAL_SNAKE_SIZE);
                    MessageBox(HDlg, messageText, TEXT("Snake"), MB_OK | MB_ICONERROR);
                    return TRUE;
                }
                    
                GetWindowText(hwndHeight, editText, 10);
                success = ReadIntFromString(editText, &editHeight);
                if (!success || editHeight <= 0 || editHeight > 127)
                {
                    MessageBox(HDlg, TEXT("Invalid field height. Please, enter a numeric value between 1 and 127."), TEXT("Snake"), MB_OK | MB_ICONERROR);
                    return TRUE;
                }
                else if (editHeight == 1 && editWidth == INITIAL_SNAKE_SIZE)
                {
                    MessageBox(HDlg, TEXT("Invalid field size, there will be no empty space for the food."), TEXT("Snake"), MB_OK | MB_ICONERROR);
                    return TRUE;
                }
                
                GetWindowText(hwndSpeed, editText, 10);
                success = ReadIntFromString(editText, &editSpeed);
                if (!success || editSpeed <= 0)
                {
                    MessageBox(HDlg, TEXT("Invalid speed. Please, enter a numeric positive value."), TEXT("Snake"), MB_OK | MB_ICONERROR);
                    return TRUE;
                }
                
                checkPassThroughWalls = (BOOL) SendMessage(hwndPassWallThrough, BM_GETCHECK, 0, 0);
                
                if (snakeState == PAUSED)
                {
                    dialogResult = MessageBox(HDlg, TEXT("Are you sure to finish the current game in order to change the preferences?"), TEXT("Snake"), MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING);
                    if (dialogResult == IDYES) SetWindowText(hwndParent, TEXT("Snake"));
                    else return TRUE;
                }
                
                EndingCleanUp();
                snakeState = IDLE;
                
                fieldWidth       = editWidth;
                fieldHeight      = editHeight;
                snakeSpeed       = editSpeed;
                passThroughWalls = checkPassThroughWalls;
                
                if ((result = Initialize(TRUE)) != SR_OK)
                {
                    CriticalEnd(hwndParent, result);
                    return TRUE;
                }
                
                InvalidateRect(hwndParent, NULL, FALSE);
                EndDialog(HDlg, 0);
            }
            else if (LOWORD(WParam) == IDCANCEL) EndDialog(HDlg, 0);
            return TRUE;
        
        case WM_CLOSE:
            EndDialog(HDlg, 0);
            return TRUE;
    }
    
    return FALSE;
}

BOOL HandleCommand(HINSTANCE HInstance, HWND HWnd, WPARAM WParam)
{
    int dialogResult;
    SNAKE_RESULT result;
    
    switch (LOWORD(WParam))
    {
        case IDM_FILE_NEW:
            if (snakeState == RUNNING || snakeState == PAUSED)
            {
                if (snakeState == RUNNING)
                {
                    KillTimer(HWnd, 1);
                    snakeState = PAUSED;
                    SetWindowText(HWnd, TEXT("Snake (Paused)"));
                }
                dialogResult = MessageBox(HWnd, TEXT("Are you sure to finish the current game and start a new one?"), TEXT("Snake"), MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING);
                if (dialogResult == IDNO) return TRUE;
            }
            EndingCleanUp();
            if ((result = Initialize(FALSE)) == SR_OK)
            {
                SetTimer(HWnd, 1, 1000 / snakeSpeed, NULL);
                InvalidateRect(HWnd, NULL, FALSE);
            }
            else CriticalEnd(HWnd, result);
            return TRUE;
            
        case IDM_FILE_EXIT:
            PostQuitMessage(0);
            return TRUE;
            
        case IDM_FILE_PREFERENCES:
            if (snakeState == RUNNING)
            {
                KillTimer(HWnd, 1);
                snakeState = PAUSED;
                SetWindowText(HWnd, TEXT("Snake (Paused)"));
            }
            DialogBox(HInstance, MAKEINTRESOURCE(IDD_PREFERENCES), HWnd, PreferencesDlgProc);
            return TRUE;
            
        case IDM_ABOUT:
            if (snakeState == RUNNING)
            {
                KillTimer(HWnd, 1);
                snakeState = PAUSED;
                SetWindowText(HWnd, TEXT("Snake (Paused)"));
            }
            DialogBox(HInstance, MAKEINTRESOURCE(IDD_ABOUTBOX), HWnd, AboutDlgProc);
            return TRUE;
            
        default:
            return FALSE;
    }
}

BOOL HandleKeyDown(HWND HWnd, WPARAM WParam)
{
    SNAKE_RESULT result;
    
    switch (WParam)
    {
        case 'D':
        case VK_RIGHT:
            if ((result = ReceiveCommand(RIGHT)) != SR_OK) CriticalEnd(HWnd, result);
            return TRUE;

        case 'W':
        case VK_UP:
            if ((result = ReceiveCommand(UP)) != SR_OK) CriticalEnd(HWnd, result);
            return TRUE;

        case 'A':
        case VK_LEFT:
            if ((result = ReceiveCommand(LEFT)) != SR_OK) CriticalEnd(HWnd, result);
            return TRUE;

        case 'S':
        case VK_DOWN:
            if ((result = ReceiveCommand(DOWN)) != SR_OK) CriticalEnd(HWnd, result);
            return TRUE;
            
        case 'P':
        case VK_SPACE:
        case VK_PAUSE:
            if (snakeState == RUNNING)
            {
                KillTimer(HWnd, 1);
                snakeState = PAUSED;
                SetWindowText(HWnd, TEXT("Snake (Paused)"));
            }
            else if (snakeState == PAUSED)
            {
                SetTimer(HWnd, 1, 1000 / snakeSpeed, NULL);
                snakeState = RUNNING;
                SetWindowText(HWnd, TEXT("Snake"));
            }
            return TRUE;
        
        default:
            return FALSE;
    }
}

BOOL ReadIntFromString(TCHAR *String, int *Result)
{
    int  result   = 0;
    int  i        = 0;
    BOOL negative = FALSE;
    TCHAR c;
    
    if (String[i] == '\0') return FALSE;
    else if (String[i] == '-')
    {
        negative = TRUE;
        i++;
    }
    
    while ((c = String[i++]) != '\0')
    {
        if (c >= '0' && c <= '9')
        {
            result *= 10;
            result += c - '0';
        }
        else return FALSE;
    }
    
    if (negative) result *= -1;
    *Result = result;
    
    return TRUE;
}

void CriticalEnd(HWND HWnd, SNAKE_RESULT Result)
{
    MessageBox(HWnd, ResultToString(Result), TEXT("Snake"), MB_ICONERROR | MB_OK);
    PostQuitMessage(0);
}
