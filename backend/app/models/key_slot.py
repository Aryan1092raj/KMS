"""KeySlot model."""
import enum
import uuid

from sqlalchemy import CheckConstraint, Enum, ForeignKey, SmallInteger, UniqueConstraint
from sqlalchemy.dialects.postgresql import UUID
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.core.database import Base


class KeyStatus(str, enum.Enum):
    available = "available"
    retrieved = "retrieved"
    maintenance = "maintenance"


class KeySlot(Base):
    __tablename__ = "key_slots"
    __table_args__ = (
        UniqueConstraint("device_id", "slot_number"),
        CheckConstraint("slot_number BETWEEN 1 AND 8", name="slot_number_range"),
    )

    id: Mapped[uuid.UUID] = mapped_column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    device_id: Mapped[uuid.UUID] = mapped_column(UUID(as_uuid=True), ForeignKey("devices.id"), nullable=False)
    slot_number: Mapped[int] = mapped_column(SmallInteger, nullable=False)
    room_id: Mapped[uuid.UUID | None] = mapped_column(UUID(as_uuid=True), ForeignKey("rooms.id"), nullable=True)
    status: Mapped[KeyStatus] = mapped_column(
        Enum(KeyStatus, name="key_status"), nullable=False, default=KeyStatus.available
    )

    device: Mapped["Device"] = relationship("Device", back_populates="key_slots")
    room: Mapped["Room | None"] = relationship("Room", back_populates="key_slots")
    retrieval_logs: Mapped[list["RetrievalLog"]] = relationship("RetrievalLog", back_populates="key_slot")
