// WorkItem.cpp (Oclgrind)
// Copyright (c) 2013-2015, James Price and Simon McIntosh-Smith,
// University of Bristol. All rights reserved.`
//
// This program is provided under a three-clause BSD license. For full
// license terms please see the LICENSE file distributed with this
// source code.

#include "common.h"

#include "llvm/DebugInfo.h"
#include "llvm/GlobalVariable.h"
#include "llvm/InstrTypes.h"
#include "llvm/Instruction.h"
#include "llvm/Instructions.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/Module.h"
#include "llvm/Support/InstIterator.h"

#include "Context.h"
#include "Kernel.h"
#include "KernelInvocation.h"
#include "Memory.h"
#include "Program.h"
#include "WorkGroup.h"
#include "WorkItem.h"

using namespace oclgrind;
using namespace std;

struct WorkItem::Position
{
  llvm::Function::const_iterator       prevBlock;
  llvm::Function::const_iterator       currBlock;
  llvm::Function::const_iterator       nextBlock;
  llvm::BasicBlock::const_iterator     currInst;
  std::stack<const llvm::Instruction*> callStack;
};

WorkItem::WorkItem(const KernelInvocation *kernelInvocation,
                   WorkGroup *workGroup, Size3 lid)
  : m_context(kernelInvocation->getContext()),
    m_kernelInvocation(kernelInvocation),
    m_workGroup(workGroup)
{
  m_localID = lid;

  // Compute global ID
  Size3 groupID = workGroup->getGroupID();
  Size3 groupSize = workGroup->getGroupSize();
  Size3 globalOffset = kernelInvocation->getGlobalOffset();
  m_globalID.x = lid.x + groupID.x*groupSize.x + globalOffset.x;
  m_globalID.y = lid.y + groupID.y*groupSize.y + globalOffset.y;
  m_globalID.z = lid.z + groupID.z*groupSize.z + globalOffset.z;

  Size3 globalSize = kernelInvocation->getGlobalSize();
  m_globalIndex = (m_globalID.x +
                  (m_globalID.y +
                   m_globalID.z*globalSize.y) * globalSize.x);

  const Kernel *kernel = kernelInvocation->getKernel();

  // Load interpreter cache
  m_cache = kernel->getProgram()->getInterpreterCache(kernel->getFunction());

  // Set initial number of values to store based on cache
  m_values.resize(m_cache->getNumValues());

  m_privateMemory = kernel->getPrivateMemory()->clone();

  // Initialise kernel arguments
  TypedValueMap::const_iterator argItr;
  for (argItr = kernel->args_begin(); argItr != kernel->args_end(); argItr++)
  {
    setValue(argItr->first, m_pool.clone(argItr->second));
  }

  // Initialize interpreter state
  m_state    = READY;
  m_position = new Position;
  m_position->prevBlock = NULL;
  m_position->nextBlock = NULL;
  m_position->currBlock = kernel->getFunction()->begin();
  m_position->currInst = m_position->currBlock->begin();
}

WorkItem::~WorkItem()
{
  delete m_privateMemory;
  delete m_position;
}

void WorkItem::clearBarrier()
{
  if (m_state == BARRIER)
  {
    m_state = READY;
  }
}

void WorkItem::dispatch(const llvm::Instruction *instruction,
                        TypedValue& result)
{
  switch (instruction->getOpcode())
  {
  case llvm::Instruction::Add:
    add(instruction, result);
    break;
  case llvm::Instruction::Alloca:
    alloc(instruction, result);
    break;
  case llvm::Instruction::And:
    bwand(instruction, result);
    break;
  case llvm::Instruction::AShr:
    ashr(instruction, result);
    break;
  case llvm::Instruction::BitCast:
    bitcast(instruction, result);
    break;
  case llvm::Instruction::Br:
    br(instruction, result);
    break;
  case llvm::Instruction::Call:
    call(instruction, result);
    break;
  case llvm::Instruction::ExtractElement:
    extractelem(instruction, result);
    break;
  case llvm::Instruction::ExtractValue:
    extractval(instruction, result);
    break;
  case llvm::Instruction::FAdd:
    fadd(instruction, result);
    break;
  case llvm::Instruction::FCmp:
    fcmp(instruction, result);
    break;
  case llvm::Instruction::FDiv:
    fdiv(instruction, result);
    break;
  case llvm::Instruction::FMul:
    fmul(instruction, result);
    break;
  case llvm::Instruction::FPExt:
    fpext(instruction, result);
    break;
  case llvm::Instruction::FPToSI:
    fptosi(instruction, result);
    break;
  case llvm::Instruction::FPToUI:
    fptoui(instruction, result);
    break;
  case llvm::Instruction::FPTrunc:
    fptrunc(instruction, result);
    break;
  case llvm::Instruction::FRem:
    frem(instruction, result);
    break;
  case llvm::Instruction::FSub:
    fsub(instruction, result);
    break;
  case llvm::Instruction::GetElementPtr:
    gep(instruction, result);
    break;
  case llvm::Instruction::ICmp:
    icmp(instruction, result);
    break;
  case llvm::Instruction::InsertElement:
    insertelem(instruction, result);
    break;
  case llvm::Instruction::InsertValue:
    insertval(instruction, result);
    break;
  case llvm::Instruction::IntToPtr:
    inttoptr(instruction, result);
    break;
  case llvm::Instruction::Load:
    load(instruction, result);
    break;
  case llvm::Instruction::LShr:
    lshr(instruction, result);
    break;
  case llvm::Instruction::Mul:
    mul(instruction, result);
    break;
  case llvm::Instruction::Or:
    bwor(instruction, result);
    break;
  case llvm::Instruction::PHI:
    phi(instruction, result);
    break;
  case llvm::Instruction::PtrToInt:
    ptrtoint(instruction, result);
    break;
  case llvm::Instruction::Ret:
    ret(instruction, result);
    break;
  case llvm::Instruction::SDiv:
    sdiv(instruction, result);
    break;
  case llvm::Instruction::Select:
    select(instruction, result);
    break;
  case llvm::Instruction::SExt:
    sext(instruction, result);
    break;
  case llvm::Instruction::Shl:
    shl(instruction, result);
    break;
  case llvm::Instruction::ShuffleVector:
    shuffle(instruction, result);
    break;
  case llvm::Instruction::SIToFP:
    sitofp(instruction, result);
    break;
  case llvm::Instruction::SRem:
    srem(instruction, result);
    break;
  case llvm::Instruction::Store:
    store(instruction, result);
    break;
  case llvm::Instruction::Sub:
    sub(instruction, result);
    break;
  case llvm::Instruction::Switch:
    swtch(instruction, result);
    break;
  case llvm::Instruction::Trunc:
    itrunc(instruction, result);
    break;
  case llvm::Instruction::UDiv:
    udiv(instruction, result);
    break;
  case llvm::Instruction::UIToFP:
    uitofp(instruction, result);
    break;
  case llvm::Instruction::URem:
    urem(instruction, result);
    break;
  case llvm::Instruction::Unreachable:
    FATAL_ERROR("Encountered unreachable instruction");
  case llvm::Instruction::Xor:
    bwxor(instruction, result);
    break;
  case llvm::Instruction::ZExt:
    zext(instruction, result);
    break;
  default:
    FATAL_ERROR("Unsupported instruction: %s", instruction->getOpcodeName());
  }
}

void WorkItem::execute(const llvm::Instruction *instruction)
{
  // Prepare private variable for instruction result
  pair<unsigned,unsigned> resultSize = getValueSize(instruction);

  // Prepare result
  TypedValue result = {
    resultSize.first,
    resultSize.second,
    NULL
  };
  if (result.size)
  {
    result.data = m_pool.alloc(result.size*result.num);
  }

  if (instruction->getOpcode() != llvm::Instruction::PHI &&
      m_phiTemps.size() > 0)
  {
    TypedValueMap::iterator itr;
    for (itr = m_phiTemps.begin(); itr != m_phiTemps.end(); itr++)
    {
      setValue(itr->first, itr->second);
    }
    m_phiTemps.clear();
  }

  // Execute instruction
  dispatch(instruction, result);

  // Store result
  if (result.size)
  {
    if (instruction->getOpcode() != llvm::Instruction::PHI)
    {
      setValue(instruction, result);
    }
    else
    {
      m_phiTemps[instruction] = result;
    }
  }

  m_context->notifyInstructionExecuted(this, instruction, result);
}

TypedValue WorkItem::getValue(const llvm::Value *key) const
{
  return m_values[m_cache->getValueID(key)];
}

const stack<const llvm::Instruction*>& WorkItem::getCallStack() const
{
  return m_position->callStack;
}

const llvm::Instruction* WorkItem::getCurrentInstruction() const
{
  return m_position->currInst;
}

Size3 WorkItem::getGlobalID() const
{
  return m_globalID;
}

size_t WorkItem::getGlobalIndex() const
{
  return m_globalIndex;
}

Size3 WorkItem::getLocalID() const
{
  return m_localID;
}

Memory* WorkItem::getMemory(unsigned int addrSpace) const
{
  switch (addrSpace)
  {
    case AddrSpacePrivate:
      return m_privateMemory;
    case AddrSpaceGlobal:
    case AddrSpaceConstant:
      return m_context->getGlobalMemory();
    case AddrSpaceLocal:
      return m_workGroup->getLocalMemory();
    default:
      FATAL_ERROR("Unsupported address space: %d", addrSpace);
  }
}

TypedValue WorkItem::getOperand(const llvm::Value *operand) const
{
  unsigned valID = operand->getValueID();
  if (valID == llvm::Value::ArgumentVal ||
      valID == llvm::Value::GlobalVariableVal ||
      valID >= llvm::Value::InstructionVal)
  {
    return getValue(operand);
  }
  //else if (valID == llvm::Value::BasicBlockVal)
  //{
  //}
  //else if (valID == llvm::Value::FunctionVal)
  //{
  //}
  //else if (valID == llvm::Value::GlobalAliasVal)
  //{
  //}
  //else if (valID == llvm::Value::BlockAddressVal)
  //{
  //}
  else if (valID == llvm::Value::ConstantExprVal)
  {
    pair<unsigned,unsigned> size = getValueSize(operand);
    TypedValue result;
    result.size = size.first;
    result.num  = size.second;
    result.data = m_pool.alloc(getTypeSize(operand->getType()));

    // Use of const_cast here is ugly, but ConstExpr instructions
    // shouldn't actually modify WorkItem state anyway
    const_cast<WorkItem*>(this)->dispatch(
      m_cache->getConstantExpr(operand), result);
    return result;
  }
  else if (valID == llvm::Value::UndefValueVal            ||
           valID == llvm::Value::ConstantAggregateZeroVal ||
           valID == llvm::Value::ConstantDataArrayVal     ||
           valID == llvm::Value::ConstantDataVectorVal    ||
           valID == llvm::Value::ConstantIntVal           ||
           valID == llvm::Value::ConstantFPVal            ||
           valID == llvm::Value::ConstantArrayVal         ||
           valID == llvm::Value::ConstantStructVal        ||
           valID == llvm::Value::ConstantVectorVal        ||
           valID == llvm::Value::ConstantPointerNullVal)
  {
    return m_cache->getConstant(operand);
  }
  //else if (valID == llvm::Value::MDNodeVal)
  //{
  //}
  //else if (valID == llvm::Value::MDStringVal)
  //{
  //}
  //else if (valID == llvm::Value::InlineAsmVal)
  //{
  //}
  //else if (valID == llvm::Value::PseudoSourceValueVal)
  //{
  //}
  //else if (valID == llvm::Value::FixedStackPseudoSourceValueVal)
  //{
  //}
  else
  {
    FATAL_ERROR("Unhandled operand type: %d", valID);
  }

  // Unreachable
  assert(false);
}

Memory* WorkItem::getPrivateMemory() const
{
  return m_privateMemory;
}

WorkItem::State WorkItem::getState() const
{
  return m_state;
}

const unsigned char* WorkItem::getValueData(const llvm::Value *value) const
{
  if (!hasValue(value))
  {
    return NULL;
  }
  return getValue(value).data;
}

const llvm::Value* WorkItem::getVariable(std::string name) const
{
  VariableMap::const_iterator itr;
  itr = m_variables.find(name);
  if (itr == m_variables.end())
  {
    return NULL;
  }
  return itr->second;
}

const WorkGroup* WorkItem::getWorkGroup() const
{
  return m_workGroup;
}

bool WorkItem::hasValue(const llvm::Value *key) const
{
  return m_cache->hasValue(key);
}

bool WorkItem::printValue(const llvm::Value *value) const
{
  if (!hasValue(value))
  {
    return false;
  }

  printTypedData(value->getType(), getValue(value).data);

  return true;
}

bool WorkItem::printVariable(string name) const
{
  // Find variable
  const llvm::Value *value = getVariable(name);
  if (!value)
  {
    return false;
  }

  // Get variable value
  TypedValue result = getValue(value);
  const llvm::Type *type = value->getType();

  if (((const llvm::Instruction*)value)->getOpcode()
       == llvm::Instruction::Alloca)
  {
    // If value is alloca result, look-up data at address
    const llvm::Type *elemType = value->getType()->getPointerElementType();
    size_t address = result.getPointer();

    unsigned char *data = (unsigned char*)m_privateMemory->getPointer(address);
    printTypedData(elemType, data);
  }
  else
  {
    printTypedData(type, result.data);
  }

  return true;
}

void WorkItem::setValue(const llvm::Value *key, TypedValue value)
{
  m_values[m_cache->getValueID(key)] = value;
}

WorkItem::State WorkItem::step()
{
  assert(m_state == READY);

  // Execute the next instruction
  execute(m_position->currInst);

  // Check if we've reached the end of the block
  if (++m_position->currInst == m_position->currBlock->end() ||
      m_position->nextBlock)
  {
    if (m_position->nextBlock)
    {
      // Move to next basic block
      m_position->prevBlock = m_position->currBlock;
      m_position->currBlock = m_position->nextBlock;
      m_position->nextBlock = NULL;
      m_position->currInst  = m_position->currBlock->begin();
    }
  }

  return m_state;
}


///////////////////////////////
//// Instruction execution ////
///////////////////////////////

#define INSTRUCTION(name) \
  void WorkItem::name(const llvm::Instruction *instruction, TypedValue& result)

INSTRUCTION(add)
{
  TypedValue opA = getOperand(instruction->getOperand(0));
  TypedValue opB = getOperand(instruction->getOperand(1));
  for (unsigned i = 0; i < result.num; i++)
  {
    result.setUInt(opA.getUInt(i) + opB.getUInt(i), i);
  }
}

INSTRUCTION(alloc)
{
  const llvm::AllocaInst *allocInst = ((const llvm::AllocaInst*)instruction);
  const llvm::Type *type = allocInst->getAllocatedType();

  // Perform allocation
  unsigned size = getTypeSize(type);
  size_t address = m_privateMemory->allocateBuffer(size);

  // Create pointer to alloc'd memory
  result.setPointer(address);
}

INSTRUCTION(ashr)
{
  TypedValue opA = getOperand(instruction->getOperand(0));
  TypedValue opB = getOperand(instruction->getOperand(1));
  uint64_t shiftMask =
    (result.num > 1 ? result.size : max((size_t)result.size, sizeof(uint32_t)))
    * 8 - 1;
  for (unsigned i = 0; i < result.num; i++)
  {
    result.setUInt(opA.getSInt(i) >> (opB.getUInt(i) & shiftMask), i);
  }
}

INSTRUCTION(bitcast)
{
  const llvm::Value *op = instruction->getOperand(0);

  // Check for address space casts
  if (instruction->getType()->isPointerTy())
  {
    unsigned srcAddrSpace = op->getType()->getPointerAddressSpace();
    unsigned dstAddrSpace = instruction->getType()->getPointerAddressSpace();
    if (srcAddrSpace != dstAddrSpace)
    {
      FATAL_ERROR("Invalid pointer cast from %s to %s address spaces",
                  getAddressSpaceName(srcAddrSpace),
                  getAddressSpaceName(dstAddrSpace));
    }
  }

  TypedValue operand = getOperand(op);
  memcpy(result.data, operand.data, result.size*result.num);
}

INSTRUCTION(br)
{
  if (instruction->getNumOperands() == 1)
  {
    // Unconditional branch
    m_position->nextBlock = (const llvm::BasicBlock*)instruction->getOperand(0);
  }
  else
  {
    // Conditional branch
    bool pred = getOperand(instruction->getOperand(0)).getUInt();
    const llvm::Value *iftrue = instruction->getOperand(2);
    const llvm::Value *iffalse = instruction->getOperand(1);
    m_position->nextBlock = (const llvm::BasicBlock*)(pred ? iftrue : iffalse);
  }
}

INSTRUCTION(bwand)
{
  TypedValue opA = getOperand(instruction->getOperand(0));
  TypedValue opB = getOperand(instruction->getOperand(1));
  for (unsigned i = 0; i < result.num; i++)
  {
    result.setUInt(opA.getUInt(i) & opB.getUInt(i), i);
  }
}

INSTRUCTION(bwor)
{
  TypedValue opA = getOperand(instruction->getOperand(0));
  TypedValue opB = getOperand(instruction->getOperand(1));
  for (unsigned i = 0; i < result.num; i++)
  {
    result.setUInt(opA.getUInt(i) | opB.getUInt(i), i);
  }
}

INSTRUCTION(bwxor)
{
  TypedValue opA = getOperand(instruction->getOperand(0));
  TypedValue opB = getOperand(instruction->getOperand(1));
  for (unsigned i = 0; i < result.num; i++)
  {
    result.setUInt(opA.getUInt(i) ^ opB.getUInt(i), i);
  }
}

INSTRUCTION(call)
{
  const llvm::CallInst *callInst = (const llvm::CallInst*)instruction;
  const llvm::Function *function = callInst->getCalledFunction();

  // Check for indirect function calls
  if (!callInst->getCalledFunction())
  {
    // Resolve indirect function pointer
    const llvm::Value *func = callInst->getCalledValue();
    const llvm::Value *funcPtr = ((const llvm::User*)func)->getOperand(0);
    function = (const llvm::Function*)funcPtr;
  }

  // Check if function has definition
  if (!function->isDeclaration())
  {
    m_position->callStack.push(m_position->currInst);
    m_position->nextBlock = function->begin();

    // Set function arguments
    llvm::Function::const_arg_iterator argItr;
    for (argItr = function->arg_begin();
         argItr != function->arg_end(); argItr++)
    {
      const llvm::Value *arg = callInst->getArgOperand(argItr->getArgNo());
      setValue(argItr, m_pool.clone(getOperand(arg)));
    }

    return;
  }

  // Call builtin function
  InterpreterCache::Builtin builtin = m_cache->getBuiltin(function);
  builtin.function.func(this, callInst,
                        builtin.name, builtin.overload,
                        result, builtin.function.op);
}

INSTRUCTION(extractelem)
{
  const llvm::ExtractElementInst *extract =
    (const llvm::ExtractElementInst*)instruction;
  unsigned index     = getOperand(extract->getIndexOperand()).getUInt();
  TypedValue operand = getOperand(extract->getVectorOperand());
  memcpy(result.data, operand.data + result.size*index, result.size);
}

INSTRUCTION(extractval)
{
  const llvm::ExtractValueInst *extract =
    (const llvm::ExtractValueInst*)instruction;
  const llvm::Value *agg = extract->getAggregateOperand();
  llvm::ArrayRef<unsigned int> indices = extract->getIndices();

  // Compute offset for target value
  int offset = 0;
  const llvm::Type *type = agg->getType();
  for (unsigned i = 0; i < indices.size(); i++)
  {
    if (type->isArrayTy())
    {
      type = type->getArrayElementType();
      offset += getTypeSize(type) * indices[i];
    }
    else if (type->isStructTy())
    {
      offset += getStructMemberOffset((const llvm::StructType*)type,
                                      indices[i]);
      type = type->getStructElementType(indices[i]);
    }
    else
    {
      FATAL_ERROR("Unsupported aggregate type: %d", type->getTypeID())
    }
  }

  // Copy target value to result
  memcpy(result.data, getOperand(agg).data + offset, getTypeSize(type));
}

INSTRUCTION(fadd)
{
  TypedValue opA = getOperand(instruction->getOperand(0));
  TypedValue opB = getOperand(instruction->getOperand(1));
  for (unsigned i = 0; i < result.num; i++)
  {
    result.setFloat(opA.getFloat(i) + opB.getFloat(i), i);
  }
}

INSTRUCTION(fcmp)
{
  const llvm::CmpInst *cmpInst = (const llvm::CmpInst*)instruction;
  llvm::CmpInst::Predicate pred = cmpInst->getPredicate();

  TypedValue opA = getOperand(instruction->getOperand(0));
  TypedValue opB = getOperand(instruction->getOperand(1));

  uint64_t t = result.num > 1 ? -1 : 1;
  for (unsigned i = 0; i < result.num; i++)
  {
    double a = opA.getFloat(i);
    double b = opB.getFloat(i);

    uint64_t r;
    switch (pred)
    {
    case llvm::CmpInst::FCMP_OEQ:
    case llvm::CmpInst::FCMP_UEQ:
      r = a == b;
      break;
    case llvm::CmpInst::FCMP_ONE:
    case llvm::CmpInst::FCMP_UNE:
      r = a != b;
      break;
    case llvm::CmpInst::FCMP_OGT:
    case llvm::CmpInst::FCMP_UGT:
      r = a > b;
      break;
    case llvm::CmpInst::FCMP_OGE:
    case llvm::CmpInst::FCMP_UGE:
      r = a >= b;
      break;
    case llvm::CmpInst::FCMP_OLT:
    case llvm::CmpInst::FCMP_ULT:
      r = a < b;
      break;
    case llvm::CmpInst::FCMP_OLE:
    case llvm::CmpInst::FCMP_ULE:
      r = a <= b;
      break;
    case llvm::CmpInst::FCMP_FALSE:
      r = false;
      break;
    case llvm::CmpInst::FCMP_TRUE:
      r = true;
      break;
    default:
      FATAL_ERROR("Unsupported FCmp predicate: %d", pred);
    }

    // Deal with NaN operands
    if (::isnan(a) || ::isnan(b))
    {
      r = !llvm::CmpInst::isOrdered(pred);
    }

    result.setUInt(r ? t : 0, i);
  }
}

INSTRUCTION(fdiv)
{
  TypedValue opA = getOperand(instruction->getOperand(0));
  TypedValue opB = getOperand(instruction->getOperand(1));
  for (unsigned i = 0; i < result.num; i++)
  {
    result.setFloat(opA.getFloat(i) / opB.getFloat(i), i);
  }
}

INSTRUCTION(fmul)
{
  TypedValue opA = getOperand(instruction->getOperand(0));
  TypedValue opB = getOperand(instruction->getOperand(1));
  for (unsigned i = 0; i < result.num; i++)
  {
    result.setFloat(opA.getFloat(i) * opB.getFloat(i), i);
  }
}

INSTRUCTION(fpext)
{
  TypedValue op = getOperand(instruction->getOperand(0));
  for (unsigned i = 0; i < result.num; i++)
  {
    result.setFloat(op.getFloat(i), i);
  }
}

INSTRUCTION(fptosi)
{
  TypedValue op = getOperand(instruction->getOperand(0));
  for (unsigned i = 0; i < result.num; i++)
  {
    result.setSInt((int64_t)op.getFloat(i), i);
  }
}

INSTRUCTION(fptoui)
{
  TypedValue op = getOperand(instruction->getOperand(0));
  for (unsigned i = 0; i < result.num; i++)
  {
    result.setUInt((uint64_t)op.getFloat(i), i);
  }
}

INSTRUCTION(frem)
{
  TypedValue opA = getOperand(instruction->getOperand(0));
  TypedValue opB = getOperand(instruction->getOperand(1));
  for (unsigned i = 0; i < result.num; i++)
  {
    result.setFloat(fmod(opA.getFloat(i), opB.getFloat(i)), i);
  }
}

INSTRUCTION(fptrunc)
{
  TypedValue op = getOperand(instruction->getOperand(0));
  for (unsigned i = 0; i < result.num; i++)
  {
    result.setFloat(op.getFloat(i), i);
  }
}

INSTRUCTION(fsub)
{
  TypedValue opA = getOperand(instruction->getOperand(0));
  TypedValue opB = getOperand(instruction->getOperand(1));
  for (unsigned i = 0; i < result.num; i++)
  {
    result.setFloat(opA.getFloat(i) - opB.getFloat(i), i);
  }
}

INSTRUCTION(gep)
{
  const llvm::GetElementPtrInst *gepInst =
    (const llvm::GetElementPtrInst*)instruction;

  // Get base address
  const llvm::Value *base = gepInst->getPointerOperand();
  size_t address = getOperand(base).getPointer();
  const llvm::Type *ptrType = gepInst->getPointerOperandType();

  // Iterate over indices
  llvm::User::const_op_iterator opItr;
  for (opItr = gepInst->idx_begin(); opItr != gepInst->idx_end(); opItr++)
  {
    int64_t offset = getOperand(opItr->get()).getSInt();

    if (ptrType->isPointerTy())
    {
      // Get pointer element size
      const llvm::Type *elemType = ptrType->getPointerElementType();
      address += offset*getTypeSize(elemType);
      ptrType = elemType;
    }
    else if (ptrType->isArrayTy())
    {
      // Get array element size
      const llvm::Type *elemType = ptrType->getArrayElementType();
      address += offset*getTypeSize(elemType);
      ptrType = elemType;
    }
    else if (ptrType->isVectorTy())
    {
      // Get vector element size
      const llvm::Type *elemType = ptrType->getVectorElementType();
      address += offset*getTypeSize(elemType);
      ptrType = elemType;
    }
    else if (ptrType->isStructTy())
    {
      address +=
        getStructMemberOffset((const llvm::StructType*)ptrType, offset);
      ptrType = ptrType->getStructElementType(offset);
    }
    else
    {
      FATAL_ERROR("Unsupported GEP base type: %d", ptrType->getTypeID());
    }
  }

  result.setPointer(address);
}

INSTRUCTION(icmp)
{
  const llvm::CmpInst *cmpInst = (const llvm::CmpInst*)instruction;
  llvm::CmpInst::Predicate pred = cmpInst->getPredicate();

  TypedValue opA = getOperand(instruction->getOperand(0));
  TypedValue opB = getOperand(instruction->getOperand(1));

  uint64_t t = result.num > 1 ? -1 : 1;
  for (unsigned i = 0; i < result.num; i++)
  {
    // Load operands
    uint64_t ua = opA.getUInt(i);
    uint64_t ub = opB.getUInt(i);
    int64_t  sa = opA.getSInt(i);
    int64_t  sb = opB.getSInt(i);

    uint64_t r;
    switch (pred)
    {
    case llvm::CmpInst::ICMP_EQ:
      r = ua == ub;
      break;
    case llvm::CmpInst::ICMP_NE:
      r = ua != ub;
      break;
    case llvm::CmpInst::ICMP_UGT:
      r = ua > ub;
      break;
    case llvm::CmpInst::ICMP_UGE:
      r = ua >= ub;
      break;
    case llvm::CmpInst::ICMP_ULT:
      r = ua < ub;
      break;
    case llvm::CmpInst::ICMP_ULE:
      r = ua <= ub;
      break;
    case llvm::CmpInst::ICMP_SGT:
      r = sa > sb;
      break;
    case llvm::CmpInst::ICMP_SGE:
      r = sa >= sb;
      break;
    case llvm::CmpInst::ICMP_SLT:
      r = sa < sb;
      break;
    case llvm::CmpInst::ICMP_SLE:
      r = sa <= sb;
      break;
    default:
      FATAL_ERROR("Unsupported ICmp predicate: %d", pred);
    }

    result.setUInt(r ? t : 0, i);
  }
}

INSTRUCTION(insertelem)
{
  TypedValue vector  = getOperand(instruction->getOperand(0));
  TypedValue element = getOperand(instruction->getOperand(1));
  unsigned index     = getOperand(instruction->getOperand(2)).getUInt();
  memcpy(result.data, vector.data, result.size*result.num);
  memcpy(result.data + index*result.size, element.data, result.size);
}

INSTRUCTION(insertval)
{
  const llvm::InsertValueInst *insert =
    (const llvm::InsertValueInst*)instruction;

  // Load original aggregate data
  const llvm::Value *agg = insert->getAggregateOperand();
  memcpy(result.data, getOperand(agg).data, result.size*result.num);

  // Compute offset for inserted value
  int offset = 0;
  llvm::ArrayRef<unsigned int> indices = insert->getIndices();
  const llvm::Type *type = agg->getType();
  for (unsigned i = 0; i < indices.size(); i++)
  {
    if (type->isArrayTy())
    {
      type = type->getArrayElementType();
      offset += getTypeSize(type) * indices[i];
    }
    else if (type->isStructTy())
    {
      offset += getStructMemberOffset((const llvm::StructType*)type,
                                      indices[i]);
      type = type->getStructElementType(indices[i]);
    }
    else
    {
      FATAL_ERROR("Unsupported aggregate type: %d", type->getTypeID())
    }
  }

  // Copy inserted value into result
  const llvm::Value *value = insert->getInsertedValueOperand();
  memcpy(result.data + offset, getOperand(value).data,
         getTypeSize(value->getType()));
}

INSTRUCTION(inttoptr)
{
  TypedValue op = getOperand(instruction->getOperand(0));
  for (unsigned i = 0; i < result.num; i++)
  {
    result.setPointer(op.getUInt(i), i);
  }
}

INSTRUCTION(itrunc)
{
  TypedValue op = getOperand(instruction->getOperand(0));
  for (unsigned i = 0; i < result.num; i++)
  {
    memcpy(result.data+i*result.size, op.data+i*op.size, result.size);
  }
}

INSTRUCTION(load)
{
  const llvm::LoadInst *loadInst = (const llvm::LoadInst*)instruction;
  unsigned addressSpace = loadInst->getPointerAddressSpace();
  size_t address = getOperand(loadInst->getPointerOperand()).getPointer();

  // Check address is correctly aligned
  if (address & (loadInst->getAlignment()-1))
  {
    m_context->logError("Invalid memory load - source pointer is "
                        "not aligned to the pointed type");
  }

  // Load data
  getMemory(addressSpace)->load(result.data, address, result.size*result.num);
}

INSTRUCTION(lshr)
{
  TypedValue opA = getOperand(instruction->getOperand(0));
  TypedValue opB = getOperand(instruction->getOperand(1));
  uint64_t shiftMask =
    (result.num > 1 ? result.size : max((size_t)result.size, sizeof(uint32_t)))
    * 8 - 1;
  for (unsigned i = 0; i < result.num; i++)
  {
    result.setUInt(opA.getUInt(i) >> (opB.getUInt(i) & shiftMask), i);
  }
}

INSTRUCTION(mul)
{
  TypedValue opA = getOperand(instruction->getOperand(0));
  TypedValue opB = getOperand(instruction->getOperand(1));
  for (unsigned i = 0; i < result.num; i++)
  {
    result.setUInt(opA.getUInt(i) * opB.getUInt(i), i);
  }
}

INSTRUCTION(phi)
{
  const llvm::PHINode *phiNode = (const llvm::PHINode*)instruction;
  const llvm::Value *value = phiNode->getIncomingValueForBlock(
    (const llvm::BasicBlock*)m_position->prevBlock);
  memcpy(result.data, getOperand(value).data, result.size*result.num);
}

INSTRUCTION(ptrtoint)
{
  TypedValue op = getOperand(instruction->getOperand(0));
  for (unsigned i = 0; i < result.num; i++)
  {
    result.setUInt(op.getPointer(i), i);
  }
}

INSTRUCTION(ret)
{
  const llvm::ReturnInst *retInst = (const llvm::ReturnInst*)instruction;

  if (!m_position->callStack.empty())
  {
    m_position->currInst = m_position->callStack.top();
    m_position->currBlock = m_position->currInst->getParent();
    m_position->callStack.pop();

    // Set return value
    const llvm::Value *returnVal = retInst->getReturnValue();
    if (returnVal)
    {
      setValue(m_position->currInst, m_pool.clone(getOperand(returnVal)));
    }
  }
  else
  {
    m_position->nextBlock = NULL;
    m_state = FINISHED;
    m_workGroup->notifyFinished(this);
    m_context->notifyWorkItemComplete(this);
  }
}

INSTRUCTION(sdiv)
{
  TypedValue opA = getOperand(instruction->getOperand(0));
  TypedValue opB = getOperand(instruction->getOperand(1));
  for (unsigned i = 0; i < result.num; i++)
  {
    int64_t a = opA.getSInt(i);
    int64_t b = opB.getSInt(i);
    int64_t r = 0;
    if (b && !(a == INT64_MIN && b == -1))
    {
      r = a / b;
    }
    result.setSInt(r, i);
  }
}

INSTRUCTION(select)
{
  const llvm::SelectInst *selectInst = (const llvm::SelectInst*)instruction;
  TypedValue opCondition = getOperand(selectInst->getCondition());
  for (unsigned i = 0; i < result.num; i++)
  {
    const bool cond =
      selectInst->getCondition()->getType()->isVectorTy() ?
      opCondition.getUInt(i) :
      opCondition.getUInt();
    const llvm::Value *op = cond ?
      selectInst->getTrueValue() :
      selectInst->getFalseValue();
    memcpy(result.data + i*result.size,
           getOperand(op).data + i*result.size,
           result.size);
  }
}

INSTRUCTION(sext)
{
  const llvm::Value *operand = instruction->getOperand(0);
  TypedValue value = getOperand(operand);
  for (unsigned i = 0; i < result.num; i++)
  {
    int64_t val = value.getSInt(i);
    if (operand->getType()->getPrimitiveSizeInBits() == 1)
    {
      val = val ? -1 : 0;
    }
    result.setSInt(val, i);
  }
}

INSTRUCTION(shl)
{
  TypedValue opA = getOperand(instruction->getOperand(0));
  TypedValue opB = getOperand(instruction->getOperand(1));
  uint64_t shiftMask =
    (result.num > 1 ? result.size : max((size_t)result.size, sizeof(uint32_t)))
    * 8 - 1;
  for (unsigned i = 0; i < result.num; i++)
  {
    result.setUInt(opA.getUInt(i) << (opB.getUInt(i) & shiftMask), i);
  }
}

INSTRUCTION(shuffle)
{
  const llvm::ShuffleVectorInst *shuffle =
    (const llvm::ShuffleVectorInst*)instruction;

  const llvm::Value *v1 = shuffle->getOperand(0);
  const llvm::Value *v2 = shuffle->getOperand(1);
  TypedValue mask = getOperand(shuffle->getMask());

  unsigned num = v1->getType()->getVectorNumElements();
  for (unsigned i = 0; i < result.num; i++)
  {
    if (shuffle->getMask()->getAggregateElement(i)->getValueID()
          == llvm::Value::UndefValueVal)
    {
      // Don't care / undef
      continue;
    }

    const llvm::Value *src = v1;
    unsigned int index = mask.getUInt(i);
    if (index >= num)
    {
      index -= num;
      src = v2;
    }
    memcpy(result.data + i*result.size,
           getOperand(src).data + index*result.size, result.size);
  }
}

INSTRUCTION(sitofp)
{
  TypedValue op = getOperand(instruction->getOperand(0));
  for (unsigned i = 0; i < result.num; i++)
  {
    result.setFloat(op.getSInt(i), i);
  }
}

INSTRUCTION(srem)
{
  TypedValue opA = getOperand(instruction->getOperand(0));
  TypedValue opB = getOperand(instruction->getOperand(1));
  for (unsigned i = 0; i < result.num; i++)
  {
    int64_t a = opA.getSInt(i);
    int64_t b = opB.getSInt(i);
    int64_t r = 0;
    if (b && !(a == INT64_MIN && b == -1))
    {
      r = a % b;
    }
    result.setSInt(r, i);
  }
}

INSTRUCTION(store)
{
  const llvm::StoreInst *storeInst = (const llvm::StoreInst*)instruction;
  unsigned addressSpace = storeInst->getPointerAddressSpace();
  size_t address = getOperand(storeInst->getPointerOperand()).getPointer();

  // Check address is correctly aligned
  if (address & (storeInst->getAlignment()-1))
  {
    m_context->logError("Invalid memory store - source pointer is "
                        "not aligned to the pointed type");
  }

  // Store data
  TypedValue operand = getOperand(storeInst->getValueOperand());
  getMemory(addressSpace)->store(operand.data, address,
                                 operand.size*operand.num);
}

INSTRUCTION(sub)
{
  TypedValue opA = getOperand(instruction->getOperand(0));
  TypedValue opB = getOperand(instruction->getOperand(1));
  for (unsigned i = 0; i < result.num; i++)
  {
    result.setUInt(opA.getUInt(i) - opB.getUInt(i), i);
  }
}

INSTRUCTION(swtch)
{
  const llvm::SwitchInst *swtch = (const llvm::SwitchInst*)instruction;
  const llvm::Value *cond = swtch->getCondition();
  uint64_t val = getOperand(cond).getUInt();
  const llvm::ConstantInt *cval =
    (const llvm::ConstantInt*)llvm::ConstantInt::get(cond->getType(), val);
  m_position->nextBlock = swtch->findCaseValue(cval).getCaseSuccessor();
}

INSTRUCTION(udiv)
{
  TypedValue opA = getOperand(instruction->getOperand(0));
  TypedValue opB = getOperand(instruction->getOperand(1));
  for (unsigned i = 0; i < result.num; i++)
  {
    uint64_t a = opA.getUInt(i);
    uint64_t b = opB.getUInt(i);
    result.setUInt(b ? a / b : 0, i);
  }
}

INSTRUCTION(uitofp)
{
  TypedValue op = getOperand(instruction->getOperand(0));
  for (unsigned i = 0; i < result.num; i++)
  {
    result.setFloat(op.getUInt(i), i);
  }
}

INSTRUCTION(urem)
{
  TypedValue opA = getOperand(instruction->getOperand(0));
  TypedValue opB = getOperand(instruction->getOperand(1));
  for (unsigned i = 0; i < result.num; i++)
  {
    uint64_t a = opA.getUInt(i);
    uint64_t b = opB.getUInt(i);
    result.setUInt(b ? a % b : 0, i);
  }
}

INSTRUCTION(zext)
{
  TypedValue operand = getOperand(instruction->getOperand(0));
  for (unsigned i = 0; i < result.num; i++)
  {
    result.setUInt(operand.getUInt(i), i);
  }
}

#undef INSTRUCTION


////////////////////////////////
// WorkItem::InterpreterCache //
////////////////////////////////

InterpreterCache::InterpreterCache(llvm::Function *kernel)
{
  // TODO: Determine this number dynamically?
  m_valueIDs.reserve(1024);

  // Add global variables to cache
  // TODO: Only add variables that are used?
  const llvm::Module *module = kernel->getParent();
  llvm::Module::const_global_iterator G;
  for (G = module->global_begin(); G != module->global_end(); G++)
  {
    addValueID(G);
  }


  set<llvm::Function*> processed;
  set<llvm::Function*> pending;

  pending.insert(kernel);

  while (!pending.empty())
  {
    // Get next function to process
    llvm::Function *function = *pending.begin();
    processed.insert(function);
    pending.erase(function);

    // Iterate through the function arguments
    llvm::Function::arg_iterator A;
    for (A = function->arg_begin(); A != function->arg_end(); A++)
    {
      addValueID(A);
    }

    // Iterate through instructions in function
    llvm::inst_iterator I;
    for (I = inst_begin(function); I != inst_end(function); I++)
    {
      addValueID(&*I);

      // Check for function calls
      if (I->getOpcode() == llvm::Instruction::Call)
      {
        const llvm::CallInst *call = ((const llvm::CallInst*)&*I);
        llvm::Function *callee = call->getCalledFunction();
        if (callee->isDeclaration())
        {
          // Resolve builtin function calls
          addBuiltin(callee);
        }
        else if (!processed.count(callee))
        {
          // Process called function
          pending.insert(callee);
        }
      }

      // Process operands
      for (llvm::User::value_op_iterator O = I->value_op_begin();
           O != I->value_op_end(); O++)
      {
        addOperand(*O);
      }
    }
  }
}

InterpreterCache::~InterpreterCache()
{
  ConstantMap::iterator constItr;
  for (constItr  = m_constants.begin();
       constItr != m_constants.end(); constItr++)
  {
    delete[] constItr->second.data;
  }

  ConstExprMap::iterator constExprItr;
  for (constExprItr  = m_constExpressions.begin();
       constExprItr != m_constExpressions.end(); constExprItr++)
  {
    delete constExprItr->second;
  }
}

void InterpreterCache::addBuiltin(
  const llvm::Function *function)
{
  // Check if already in cache
  InterpreterCache::BuiltinMap::iterator fItr = m_builtins.find(function);
  if (fItr != m_builtins.end())
  {
    return;
  }

  // Extract unmangled name and overload
  string name, overload;
  const string fullname = function->getName().str();
  if (fullname.compare(0,2, "_Z") == 0)
  {
    int len = atoi(fullname.c_str()+2);
    int start = fullname.find_first_not_of("0123456789", 2);
    name = fullname.substr(start, len);
    overload = fullname.substr(start + len);
  }
  else
  {
    name = fullname;
    overload = "";
  }

  // Find builtin function in map
  BuiltinFunctionMap::iterator bItr = workItemBuiltins.find(name);
  if (bItr != workItemBuiltins.end())
  {
    // Add builtin to cache
    const InterpreterCache::Builtin builtin = {bItr->second, name, overload};
    m_builtins[function] = builtin;
    return;
  }

  // Check for builtin with matching prefix
  BuiltinFunctionPrefixList::iterator pItr;
  for (pItr = workItemPrefixBuiltins.begin();
       pItr != workItemPrefixBuiltins.end(); pItr++)
  {
    if (name.compare(0, pItr->first.length(), pItr->first) == 0)
    {
      // Add builtin to cache
      const InterpreterCache::Builtin builtin = {pItr->second, name, overload};
      m_builtins[function] = builtin;
      return;
    }
  }

  // Function didn't match any builtins
  FATAL_ERROR("Undefined builtin function: %s", name.c_str());
}

InterpreterCache::Builtin InterpreterCache::getBuiltin(
  const llvm::Function *function) const
{
  return m_builtins.at(function);
}

void InterpreterCache::addConstant(const llvm::Value *value)
{
  // Check if constant already in cache
  if (m_constants.count(value))
  {
    return;
  }

  // Create constant and add to cache
  pair<unsigned,unsigned> size = getValueSize(value);
  TypedValue constant;
  constant.size = size.first;
  constant.num  = size.second;
  constant.data = new unsigned char[getTypeSize(value->getType())];
  getConstantData(constant.data, (const llvm::Constant*)value);

  m_constants[value] = constant;
}

TypedValue InterpreterCache::getConstant(const llvm::Value *operand) const
{
  ConstantMap::const_iterator itr = m_constants.find(operand);
  if (itr == m_constants.end())
  {
    FATAL_ERROR("Constant not found in cache (ID %d)", operand->getValueID());
  }
  return itr->second;
}

const llvm::Instruction* InterpreterCache::getConstantExpr(
  const llvm::Value *expr) const
{
  ConstExprMap::const_iterator itr = m_constExpressions.find(expr);
  if (itr == m_constExpressions.end())
  {
    FATAL_ERROR("Constant expression not found in cache");
  }
  return itr->second;
}

unsigned InterpreterCache::addValueID(const llvm::Value *value)
{
  ValueMap::iterator itr = m_valueIDs.find(value);
  if (itr == m_valueIDs.end())
  {
    // Assign next index to value
    unsigned pos = m_valueIDs.size();
    itr = m_valueIDs.insert(make_pair(value, pos)).first;
  }
  return itr->second;
}

unsigned InterpreterCache::getValueID(const llvm::Value *value) const
{
  ValueMap::const_iterator itr = m_valueIDs.find(value);
  if (itr == m_valueIDs.end())
  {
    FATAL_ERROR("Value not found in cache (ID %d)", value->getValueID());
  }
  return itr->second;
}

unsigned InterpreterCache::getNumValues() const
{
  return m_valueIDs.size();
}

bool InterpreterCache::hasValue(const llvm::Value *value) const
{
  return m_valueIDs.count(value);
}

void InterpreterCache::addOperand(const llvm::Value *operand)
{
  addValueID(operand);

  // Resolve constants
  if (operand->getValueID() == llvm::Value::UndefValueVal            ||
      operand->getValueID() == llvm::Value::ConstantAggregateZeroVal ||
      operand->getValueID() == llvm::Value::ConstantDataArrayVal     ||
      operand->getValueID() == llvm::Value::ConstantDataVectorVal    ||
      operand->getValueID() == llvm::Value::ConstantIntVal           ||
      operand->getValueID() == llvm::Value::ConstantFPVal            ||
      operand->getValueID() == llvm::Value::ConstantArrayVal         ||
      operand->getValueID() == llvm::Value::ConstantStructVal        ||
      operand->getValueID() == llvm::Value::ConstantVectorVal        ||
      operand->getValueID() == llvm::Value::ConstantPointerNullVal)
  {
    addConstant(operand);
  }
  else if (operand->getValueID() == llvm::Value::ConstantExprVal)
  {
    // Resolve constant expressions
    const llvm::ConstantExpr *expr = (const llvm::ConstantExpr*)operand;
    if (!m_constExpressions.count(expr))
    {
      for (llvm::User::const_op_iterator O = expr->op_begin();
           O != expr->op_end(); O++)
      {
        addOperand(*O);
      }
      m_constExpressions[expr] = getConstExprAsInstruction(expr);
      // TODO: Resolve actual value?
    }
  }
}


//////////////////////////
// WorkItem::MemoryPool //
//////////////////////////

WorkItem::MemoryPool::MemoryPool(size_t blockSize) : m_blockSize(blockSize)
{
  // Force first allocation to create new block
  m_offset = m_blockSize;
}

WorkItem::MemoryPool::~MemoryPool()
{
  list<unsigned char*>::iterator itr;
  for (itr = m_blocks.begin(); itr != m_blocks.end(); itr++)
  {
    delete[] *itr;
  }
}

unsigned char* WorkItem::MemoryPool::alloc(size_t size)
{
  // Check if requested size larger than block size
  if (size > m_blockSize)
  {
    // Oversized buffers allocated separately from main pool
    unsigned char *buffer = new unsigned char[size];
    m_blocks.push_back(buffer);
    return buffer;
  }

  // Check if enough space in current block
  if (m_offset + size > m_blockSize)
  {
    // Allocate new block
    m_blocks.push_front(new unsigned char[m_blockSize]);
    m_offset = 0;
  }
  unsigned char *buffer = m_blocks.front() + m_offset;
  m_offset += size;
  return buffer;
}

TypedValue WorkItem::MemoryPool::clone(const TypedValue& source)
{
  TypedValue dest;
  dest.size = source.size;
  dest.num = source.num;
  dest.data = alloc(dest.size*dest.num);
  memcpy(dest.data, source.data, dest.size*dest.num);
  return dest;
}
